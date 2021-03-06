module M = Map.Make(String);
module MDM = Module.DependencyMap;
module MLSet = Module.LocationSet;
module MLMap = Module.LocationMap;
module StringSet = Set.Make(String);
module FS = FastpackUtil.FS;
module Scope = FastpackUtil.Scope;

let emit_module_files = (ctx: Context.t, m: Module.t) =>
  Lwt_list.iter_s(
    ((filename, content)) => {
      let path = FS.abs_path(ctx.tmpOutputDir, filename);
      let%lwt () = FS.makedirs(FilePath.dirname(path));
      Lwt_io.(
        with_file(
          ~mode=Lwt_io.Output,
          ~perm=0o640,
          ~flags=Unix.[O_CREAT, O_TRUNC, O_RDWR],
          path,
          ch =>
          write(ch, content)
        )
      );
    },
    m.files,
  );

let runtimeMain = (envVar, publicPath) =>
  Printf.sprintf(
    {|
global = this;
global.process = global.process || {};
global.process.env = global.process.env || {};
%s
global.process.browser = true;
if(!global.Buffer) {
  global.Buffer = function() {
    throw Error("Buffer is not included in the browser environment. Consider using the polyfill")
  };
  global.Buffer.isBuffer = function() {return false};
}
if(!global.setImmediate) {
  global.setImmediate = setTimeout;
  global.clearImmediate = clearTimeout;
}
// This function is a modified version of the one created by the Webpack project
(function(modules) {
  // The module cache
  var installedModules = {};

  function __fastpack_require__(fromModule, request) {
    var moduleId =
      fromModule === null ? request : modules[fromModule].d[request];

    if (installedModules[moduleId]) {
      return installedModules[moduleId].exports;
    }
    var module = (installedModules[moduleId] = {
      id: moduleId,
      l: false,
      exports: {}
    });

    var r = __fastpack_require__.bind(null, moduleId);
    var helpers = Object.getOwnPropertyNames(__fastpack_require__.helpers);
    for (var i = 0, l = helpers.length; i < l; i++) {
      r[helpers[i]] = __fastpack_require__.helpers[helpers[i]];
    }
    r.imp = r.imp.bind(null, moduleId);
    r.state = state;
    modules[moduleId].m.call(
      module.exports,
      module,
      module.exports,
      r,
      r.imp
    );

    // Flag the module as loaded
    module.l = true;

    // Return the exports of the module
    return module.exports;
  }

  var loadedChunks = {};
  var state = {
    publicPath: %s
  };

  global.__fastpack_update_modules__ = function(newModules) {
    for (var id in newModules) {
      if (modules[id]) {
        throw new Error(
          "Chunk tries to replace already existing module: " + id
        );
      } else {
        modules[id] = newModules[id];
      }
    }
  };

  __fastpack_require__.helpers = {
    omitDefault: function(moduleVar) {
      var keys = Object.keys(moduleVar);
      var ret = {};
      for (var i = 0, l = keys.length; i < l; i++) {
        var key = keys[i];
        if (key !== "default") {
          ret[key] = moduleVar[key];
        }
      }
      return ret;
    },

    default: function(exports) {
      return exports.__esModule ? exports.default : exports;
    },

    imp: function(fromModule, request) {
      if (!global.Promise) {
        throw Error("global.Promise is undefined, consider using a polyfill");
      }
      var sourceModule = modules[fromModule];
      var chunks = (sourceModule.c || {})[request] || [];
      var promises = [];
      for (var i = 0, l = chunks.length; i < l; i++) {
        var js = chunks[i];
        var p = loadedChunks[js];
        if (!p) {
          p = loadedChunks[js] = new Promise(function(resolve, reject) {
            var script = document.createElement("script");
            script.onload = function() {
              setTimeout(resolve);
            };
            script.onerror = function() {
              reject();
              throw new Error("Script load error: " + script.src);
            };
            script.src = state.publicPath + chunks[i];
            document.head.append(script);
          });
          promises.push(p);
        }
      }
      return Promise.all(promises).then(function() {
        return __fastpack_require__(fromModule, request);
      });
    }
  };

  return __fastpack_require__(null, (__fastpack_require__.s = "$fp$main"));
}) /* --runtimeMain-- */
|},
    String.concat(
      "\n",
      List.map(
        ((name, value)) =>
          Printf.sprintf(
            "process.env[%s] = %s;",
            Yojson.to_string(`String(name)),
            Yojson.to_string(`String(value)),
          ),
        M.bindings(envVar),
      ),
    ),
    Yojson.to_string(`String(publicPath)),
  );

let runtimeChunk = "global.__fastpack_update_modules__";

type t = {
  graph: DependencyGraph.t,
  chunkRequests: MDM.t(chunkName),
  locationToChunk: Hashtbl.t(Module.location, chunkName),
  chunks: Hashtbl.t(chunkName, chunk),
  chunkDependency: Hashtbl.t(chunkName, string),
  emittedFiles: Hashtbl.t(string, file),
}
and chunkName =
  | Main
  | Named(string)
and chunk = {
  name: string,
  modules: Module.Set.t,
  dependencies: list(string),
}
and file = {
  absPath: string,
  relPath: string,
  size: int,
};

type chunkRequest = {
  depRequest: Module.Dependency.t,
  toModule: Module.t,
};

let empty = () => {
  graph: DependencyGraph.empty(),
  chunkRequests: MDM.empty,
  locationToChunk: Hashtbl.create(5000),
  chunks: Hashtbl.create(500),
  chunkDependency: Hashtbl.create(500),
  emittedFiles: Hashtbl.create(500),
};

let make = (graph: DependencyGraph.t, entry: Module.location) => {
  let bundle = {
    graph,
    chunkRequests: MDM.empty,
    locationToChunk: Hashtbl.create(5000),
    chunks: Hashtbl.create(500),
    chunkDependency: Hashtbl.create(500),
    emittedFiles: Hashtbl.create(500),
  };

  let staticChunk = ref(MLMap.empty);

  let makeChunk = (entry, seen: Module.Set.t) => {
    let rec getStaticChunk = (seen: Module.Set.t, m: Module.t) =>
      Module.Set.mem(m, seen) ?
        (Module.Set.empty, seen) :
        (
          switch (MLMap.get(m.location, staticChunk^)) {
          | Some(modules) =>
            let modules = Module.Set.diff(modules, seen);
            let seen = Module.Set.union(modules, seen);
            (modules, seen);
          | None =>
            /* let m = */
            /*   switch (DependencyGraph.lookup_module(graph, location)) { */
            /*   | Some(m) => m */
            /*   | None => */
            /*     Error.ie( */
            /*       Module.location_to_string(location) */
            /*       ++ " not found in the graph", */
            /*     ) */
            /*   }; */
            let seen = Module.Set.add(m, seen);
            let (modules, seen) =
              Sequence.fold(
                ((modules, seen), (_, m)) => {
                  let (modules', seen) = getStaticChunk(seen, m);
                  (Module.Set.union(modules, modules'), seen);
                },
                (Module.Set.singleton(m), seen),
                DependencyGraph.lookup_dependencies(~kind=`Static, graph, m),
              );
            /* let modules = [m, ...modules]; */
            staticChunk := MLMap.add(m.location, modules, staticChunk^);
            (modules, seen);
          }
        );
    let m =
      switch (DependencyGraph.lookup_module(graph, entry)) {
      | Some(m) => m
      | None =>
        Error.ie(
          Module.location_to_string(entry) ++ " not found in the graph",
        )
      };

    let (modules, seen') = getStaticChunk(Module.Set.empty, m);
    let modules = Module.Set.diff(modules, seen);
    /* List.filter(m => !MLSet.mem(m.Module.location, seen), modules); */
    let chunkRequests =
      Module.Set.fold(
        (m, acc) => {
          let chunkRequests =
            DependencyGraph.lookup_dependencies(~kind=`Dynamic, graph, m)
            |> Sequence.map(((depRequest, m')) =>
                 {depRequest, toModule: m'}
               );
          Sequence.append(acc, chunkRequests);
        },
        modules,
        Sequence.of_list([]),
      );
    (modules, chunkRequests, Module.Set.union(seen, seen'));
    /* HERE! */
    /* let rec addModule = (seen, location: Module.location) => */
    /*   MLSet.mem(location, seen) ? */
    /*     Lwt.return(([], [], seen)) : */
    /*     { */
    /*       let%lwt m = */
    /*         switch (DependencyGraph.lookup_module(graph, location)) { */
    /*         | Some(m) => m */
    /*         | None => */
    /*           Error.ie( */
    /*             Module.location_to_string(location) */
    /*             ++ " not found in the graph", */
    /*           ) */
    /*         }; */
    /*       let seen = MLSet.add(m.location, seen); */
    /*       let%lwt (modules, chunkRequests, seen) = */
    /*         Lwt_list.fold_left_s( */
    /*           ((modules, chunkRequests, seen), (_, m)) => */
    /*             switch (m) { */
    /*             | None => failwith("Should not happen") */
    /*             | Some(m) => */
    /*               let%lwt m = m; */
    /*               /1* Logs.debug(x => x("Dep: %s", m.Module.id)); *1/ */
    /*               let%lwt (modules', chunkRequests', seen) = */
    /*                 addModule(seen, m.Module.location); */
    /*               Lwt.return(( */
    /*                 modules @ modules', */
    /*                 chunkRequests' @ chunkRequests, */
    /*                 seen, */
    /*               )); */
    /*             }, */
    /*           ([], [], seen), */
    /*           DependencyGraph.lookup_dependencies(~kind=`Static, graph, m) */
    /*           |> List.rev, */
    /*         ); */
    /*       let%lwt chunkRequests' = */
    /*         DependencyGraph.lookup_dependencies(~kind=`Dynamic, graph, m) */
    /*         |> List.rev */
    /*         |> Lwt_list.map_s(((depRequest, m')) => */
    /*              switch (m') { */
    /*              | None => failwith("Should not happen") */
    /*              | Some(m') => */
    /*                let%lwt m' = m'; */
    /*                Lwt.return({depRequest, toLocation: m'.Module.location}); */
    /*              } */
    /*            ); */
    /*       Lwt.return(( */
    /*         [m, ...modules], */
    /*         chunkRequests' @ chunkRequests, */
    /*         seen, */
    /*       )); */
    /*     }; */
    /* let%lwt (modules, chunkRequests, seen) = addModule(seen, entry); */
    /* Lwt.return((List.rev(modules), chunkRequests, seen)); */
  };

  let nextChunkName = () => {
    let length = Hashtbl.length(bundle.chunks);
    length > 0 ? string_of_int(length) ++ ".js" : "";
  };

  let updateLocationHash = (modules: Module.Set.t, chunkName: chunkName) =>
    Module.Set.iter(
      m =>
        Hashtbl.replace(bundle.locationToChunk, m.Module.location, chunkName),
      modules,
    );

  let splitChunk = (name: string, modules: Module.Set.t) => {
    Logs.debug(x => x("Split chunk: %s", name));
    switch (Hashtbl.find_all(bundle.chunks, Named(name))) {
    | [{modules: chunkModules, dependencies, _}] =>
      let chunkModules = Module.Set.diff(chunkModules, modules);
      Module.Set.is_empty(chunkModules) ?
        name :
        {
          /* add new chunk */
          let newName = nextChunkName();
          Hashtbl.replace(
            bundle.chunks,
            Named(newName),
            {name: newName, modules, dependencies: []},
          );
          updateLocationHash(modules, Named(newName));
          /* modify old chunk */
          Hashtbl.replace(
            bundle.chunks,
            Named(name),
            {
              name,
              modules: chunkModules,
              dependencies: [newName, ...dependencies],
            },
          );
          updateLocationHash(chunkModules, Named(name));
          newName;
        };
    | _ => failwith("One chunk is expected")
    };
  };

  let addMainChunk = (modules: Module.Set.t) => {
    Hashtbl.replace(
      bundle.chunks,
      Main,
      {name: "", modules, dependencies: []},
    );
    updateLocationHash(modules, Main);
    Main;
  };

  let addNamedChunk = (name: string, modules: Module.Set.t) => {
    /* 0. add new chunk right away */
    Hashtbl.replace(
      bundle.chunks,
      Named(name),
      {name, modules: Module.Set.empty, dependencies: []},
    );
    let nextGroup = modules =>
      Module.Set.fold(
        (m, (groupName, group, rest)) =>
          switch (
            Hashtbl.find_all(bundle.locationToChunk, m.Module.location),
            groupName,
          ) {
          | ([], _) => (groupName, group, Module.Set.add(m, rest))
          | ([Named(name)], None) => (
              Some(name),
              Module.Set.add(m, group),
              rest,
            )
          | ([Named(name)], Some(groupName)) =>
            name == groupName ?
              (Some(groupName), Module.Set.add(m, group), rest) :
              (Some(groupName), group, Module.Set.add(m, rest))
          | ([Main], _) => failwith("Cannot split main chunk")
          | _ => failwith("Module references more than 1 chunk")
          },
        modules,
        (None, Module.Set.empty, Module.Set.empty),
      );
    let rec splitModules = modules => {
      let (name, group, rest) = nextGroup(modules);
      switch (name) {
      | Some(name) =>
        let newChunk = splitChunk(name, group);
        let (rest, dependencies) = splitModules(rest);
        (rest, [newChunk, ...dependencies]);
      | None => (rest, [])
      };
    };
    let (modules, dependencies) = splitModules(modules);
    Hashtbl.replace(
      bundle.chunks,
      Named(name),
      {name, modules, dependencies},
    );
    updateLocationHash(modules, Named(name));
    Named(name);
  };

  let addChunk = (modules: Module.Set.t) => {
    let name = nextChunkName();
    name == "" ? addMainChunk(modules) : addNamedChunk(name, modules);
  };

  let rec make' = (~chunkReqMap=MDM.empty, ~seen=Module.Set.empty, location) => {
    /* Logs.debug(x => x("Make chunk")); */
    let t = Unix.gettimeofday();
    let (modules, chunkRequests, seen) = makeChunk(location, seen);
    Logs.debug(x => x("makeChunk: %.3f", Unix.gettimeofday() -. t));
    let chunkName = addChunk(modules);
    let chunkReqMap =
      Sequence.fold(
        (chunkReqMap, {depRequest, toModule: m}) => {
          let (chunkName, chunkReqMap) =
            make'(~chunkReqMap, ~seen, m.Module.location);
          MDM.add(depRequest, chunkName, chunkReqMap);
        },
        chunkReqMap,
        Sequence.filter(
          ({toModule, _}) => !Module.Set.mem(toModule, seen),
          chunkRequests,
        ),
      );
    (chunkName, chunkReqMap);
  };
  let (_, chunkRequests) = make'(entry);
  let bundle = {...bundle, chunkRequests};

  /*   List.iter( */
  /*     ((name, chunk)) => */
  /*       Logs.debug(x => */
  /*         x( */
  /*           "chunk: %s %d", */
  /*           switch (name) { */
  /*           | Main => "main" */
  /*           | Named(n) => n */
  /*           }, */
  /*           List.length(chunk.modules), */
  /*         ) */
  /*       ), */
  /*     CCHashtbl.to_list(bundle.chunks), */
  /*   ); */
  bundle;
};

let rec getChunkDependencies =
        (~seen=StringSet.empty, chunkName: chunkName, bundle: t) =>
  switch (Hashtbl.find_all(bundle.chunkDependency, chunkName)) {
  | [] =>
    switch (chunkName) {
    | Main => failwith("Unexpected dependency on the main chunk")
    | Named(name) =>
      switch (Hashtbl.find_all(bundle.chunks, chunkName)) {
      | [] => failwith("Unknown chunk: " ++ name)
      | [{modules, dependencies, _}] =>
        if (StringSet.mem(name, seen)) {
          failwith("Chunk dependency cycle");
        } else {
          let seen = StringSet.add(name, seen);
          let dependencies =
            List.fold_left(
              (set, dep) =>
                StringSet.union(
                  set,
                  StringSet.of_list(
                    getChunkDependencies(~seen, Named(dep), bundle),
                  ),
                ),
              !Module.Set.is_empty(modules) ?
                StringSet.singleton(name) : StringSet.empty,
              dependencies,
            )
            |> StringSet.elements;
          List.iter(
            dep => Hashtbl.add(bundle.chunkDependency, chunkName, dep),
            dependencies,
          );
          dependencies;
        }
      | _ => failwith("Several chunks named: " ++ name)
      }
    }
  | found => found
  };

/* let cleanupGraph = (bundle: t) => { */
/*   let allModules = */
/*     Sequence.fold( */
/*       (allModules, chunk) => Module.Set.union(allModules, chunk.modules), */
/*       Module.Set.empty, */
/*       CCHashtbl.values(bundle.chunks), */
/*     ) |> Module.Set.map; */
/*   (); */
/* }; */

let emit = (ctx: Context.t, bundle: t) => {
  let outputDir = Config.outputDir(ctx.config);
  let outputFilename = Config.outputFilename(ctx.config);
  let envVar = Config.envVar(ctx.config);
  let publicPath = Config.publicPath(ctx.config);
  let projectRootDir = Config.projectRootDir(ctx.config);
  /* more here */
  let%lwt emittedModules =
    Sequence.fold(
      (emittedModules, chunk) => {
        let%lwt emittedModules = emittedModules;
        let (chunkName, chunk) = chunk;
        let (dirname, basename) = {
          let filename =
            FS.abs_path(
              ctx.tmpOutputDir,
              FS.relative_path(outputDir, outputFilename),
            );
          (FilePath.dirname(filename), FilePath.basename(filename));
        };
        let%lwt () = FS.makedirs(dirname);
        let filename =
          switch (chunkName) {
          | Main => FS.abs_path(dirname, basename)
          | Named(filename) => FS.abs_path(dirname, filename)
          };
        Lwt_io.with_file(
          ~mode=Lwt_io.Output,
          ~perm=0o644,
          ~flags=Unix.[O_CREAT, O_TRUNC, O_RDWR],
          filename,
          ch => {
            let emit = bytes => Lwt_io.write(ch, bytes);
            let%lwt () =
              emit(
                switch (chunkName) {
                | Main => runtimeMain(envVar, publicPath)
                | Named(_name) => runtimeChunk
                },
              );
            let%lwt () = emit("({\n");
            let%lwt emittedModules =
              Lwt_list.fold_left_s(
                (emittedModules, m) => {
                  let%lwt () = emit_module_files(ctx, m);
                  let short_str =
                    Module.location_to_short_string(
                      ~base_dir=Some(projectRootDir),
                      m.location,
                    );
                  let%lwt () =
                    Printf.sprintf(
                      "/* !s: %s */\n%s:{m:function(module, exports, __fastpack_require__) {\n",
                      CCString.replace(~sub="\\", ~by="/", short_str),
                      Yojson.to_string(`String(m.id)),
                    )
                    |> emit;
                  let%lwt () = emit("eval(\"");
                  let%lwt () = emit(m.Module.source);
                  let%lwt () = emit("\");");

                  let jsonDependencies =
                    Sequence.map(
                      (({Module.Dependency.encodedRequest, _}, m)) => (
                        encodedRequest,
                        `String(m.Module.id),
                      ),
                      DependencyGraph.lookup_dependencies(
                        ~kind=`All,
                        bundle.graph,
                        m,
                      ),
                    )
                    |> Sequence.to_list;

                  let chunkDependencies =
                    Sequence.filter_map(
                      ((dep, _)) =>
                        switch (MDM.get(dep, bundle.chunkRequests)) {
                        | None => None
                        | Some(chunkName) =>
                          switch (getChunkDependencies(chunkName, bundle)) {
                          | [] => None
                          | deps =>
                            let dirname = FilePath.dirname(filename);
                            let prefix =
                              FilePath.make_relative(
                                ctx.tmpOutputDir,
                                dirname,
                              );
                            Some((
                              dep.Module.Dependency.encodedRequest,
                              `List(
                                List.map(
                                  s =>
                                    `String(
                                      CCString.replace(
                                        ~sub="\\",
                                        ~by="/",
                                        prefix == "" ? s : prefix ++ "/" ++ s,
                                      ),
                                    ),
                                  deps,
                                ),
                              ),
                            ));
                          }
                        },
                      DependencyGraph.lookup_dependencies(
                        ~kind=`Dynamic,
                        bundle.graph,
                        m,
                      ),
                    )
                    |> Sequence.to_list;

                  let chunkData =
                    switch (chunkDependencies) {
                    | [] => ""
                    | _ =>
                      ",\nc: " ++ Yojson.to_string(`Assoc(chunkDependencies))
                    };

                  let%lwt () =
                    emit(
                      Printf.sprintf(
                        "\n},\nd: %s%s",
                        Yojson.to_string(`Assoc(jsonDependencies)),
                        chunkData,
                      ),
                    );
                  Cache.addModule(m, ctx.cache);
                  let%lwt () = emit("\n},\n");
                  Lwt.return(MLSet.add(m.location, emittedModules));
                },
                emittedModules,
                chunk.modules |> Module.Set.elements,
              );
            let%lwt () = emit("\n});\n");
            /* save the fact that chunk was emitted */
            let relPath = FilePath.make_relative(ctx.tmpOutputDir, filename);
            let absPath = FS.abs_path(outputDir, relPath);
            Hashtbl.replace(
              bundle.emittedFiles,
              absPath,
              {absPath, relPath, size: Lwt_io.position(ch) |> Int64.to_int},
            );
            Lwt.return(emittedModules);
          },
        );
      },
      Lwt.return(MLSet.empty),
      CCHashtbl.to_seq(bundle.chunks),
    );
  let _ = DependencyGraph.cleanup(bundle.graph, emittedModules);
  let%lwt () =
    switch%lwt (FS.stat_option(outputDir)) {
    | Some({st_kind: Lwt_unix.S_DIR, _}) => FS.rmdir(outputDir)
    | Some(_) => Lwt_unix.unlink(outputDir)
    | None => Lwt.return_unit
    };
  Lwt_unix.rename(ctx.tmpOutputDir, outputDir);
};

let getGraph = ({graph, _}: t) => graph;

let getFiles = (bundle: t) =>
  List.map(snd, CCHashtbl.to_list(bundle.emittedFiles));

let getTotalSize = (bundle: t) =>
  List.fold_left((acc, {size, _}) => acc + size, 0, getFiles(bundle));

let getWarnings = ({graph, _}: t) => {
  let warnings =
    DependencyGraph.foldModules(
      graph,
      (warnings, m) => warnings @ m.Module.warnings,
      [],
    );
  switch (warnings) {
  | [] => None
  | _ => Some(String.concat("\n", List.sort(compare, warnings)))
  };
};
