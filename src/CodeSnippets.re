
let codeBlockPrefix = "DOCRE_CODE_BLOCK_";

let (/+) = Filename.concat;

type codeContext = Normal | Node | Window | Iframe | Canvas;
type codeOptions = {
  context: codeContext,
  shouldParseFail: bool,
  shouldTypeFail: bool,
  shouldRaise: bool,
  dontRun: bool,
  isolate: bool,
  sharedAs: option(string),
  uses: list(string),
  hide: bool,
};

let matchOption = (text, option) => if (Str.string_match(Str.regexp("^" ++ option ++ "(\([^)]+\))$"), text, 0)) {
  Some(Str.matched_group(1, text));
} else {
  None
};

let parseCodeOptions = lang => {
  let parts = Str.split(Str.regexp_string(";"), lang);
  if (List.mem("skip", parts)
  || List.mem("bash", parts)
  || List.mem("txt", parts)
  || List.mem("js", parts)
  || List.mem("javascript", parts)
  || List.mem("sh", parts)) {
    None
  } else {
    Some(List.fold_left((options, item) => {
      switch item {
      | "window" => {...options, context: Window}
      | "canvas" => {...options, context: Canvas}
      | "iframe" => {...options, context: Iframe}

      | "raises" => {...options, shouldRaise: true}
      | "parse-fail" => {...options, shouldParseFail: true}
      | "type-fail" => {...options, shouldTypeFail: true}
      | "isolate" => {...options, isolate: true}
      | "dont-run" => {...options, dontRun: true}
      | "hide" => {...options, hide: true}
      | "reason" => options
      | _ => {
        switch (matchOption(item, "shared")) {
        | Some(name) => {...options, sharedAs: Some(name)}
        | None => switch (matchOption(item, "use")) {
        | Some(name) => {...options, uses: [name, ...options.uses]}
        | None => {

          print_endline("Skipping unexpected code option: " ++ item);
          options

        }
        }
        }
      }
      }
    }, {
      context: Normal,
      shouldParseFail: false,
      shouldTypeFail: false,
      shouldRaise: false,
      dontRun: false,
      isolate: false,
      sharedAs: None,
      uses: [],
      hide: false,
    }, parts))
  }
};

let highlight = (lang, content, cmt, js, error) => {
  "<pre class='code'><code>" ++ CodeHighlight.highlight(content, cmt) ++ "</code></pre>" ++ (switch error {
  | None => ""
  | Some(err) => "<div class='compile-error'>" ++ Omd_utils.htmlentities(err) ++ "</div>"
  })
};

/** TODO only remove from the first consecutive lines. */
let removeHashes = text => Str.global_replace(Str.regexp("^# "), "  ", text);

let getCodeBlocks = (markdowns, cmts) => {
  let codeBlocks = ref((0, []));
  let addBlock = (el, fileName, lang, contents) => {
    let options = parseCodeOptions(lang);
    switch (options) {
    | None => ()
    | Some(options) => {
      let (id, blocks) = codeBlocks^;
      codeBlocks := (id + 1, [(el, id, fileName, options, contents), ...blocks]);
    }
    }
  };

  let collect = (fileName, md) => Omd.Representation.visit(el => switch el {
    | Omd.Code_block(lang, contents) => {
      addBlock(el, fileName, lang, contents);
      None
    }
    | _ => None
  }, md) |> ignore;

  markdowns |> List.iter(((path, contents, name)) => {
    collect(path, contents);
  });

  cmts |> List.iter(((name, cmt, _, topDoc, allDocs)) => {
    Infix.(topDoc |?>> collect(cmt) |> ignore);
    allDocs |> List.iter(PrepareDocs.iter(((name, docString, _)) => {
      switch docString {
      | None => ()
      | Some(docString) => collect(cmt ++ " > " ++ name, docString)
      }
    }))
  });

  codeBlocks^ |> snd
};

open Infix;

let refmtCommand = (base, re) => {
  Printf.sprintf({|cat %s | %s --print binary > %s.ast && %s %s.ast %s_ppx.ast|},
  re,
  base /+ "node_modules/bs-platform/lib/refmt.exe",
  re,
  base /+ "node_modules/bs-platform/lib/reactjs_jsx_ppx_2.exe",
  re,
  re
  )
};

let justBscCommand = (base, re, includes) => {
  Printf.sprintf(
    {|%s %s -impl %s|},
    base /+ "node_modules/.bin/bsc",
    includes |> List.map(Printf.sprintf("-I %S")) |> String.concat(" "),
    re
  )
};

let bscCommand = (base, re, includes) => {
  Printf.sprintf(
    {|%s -pp '%s --print binary' -ppx '%s' %s -impl %s|},
    base /+ "node_modules/.bin/bsc",
    base /+ "node_modules/bs-platform/lib/refmt.exe",
    base /+ "node_modules/bs-platform/lib/reactjs_jsx_ppx_2.exe",
    includes |> List.map(Printf.sprintf("-I %S")) |> String.concat(" "),
    re
  )
};

let compileSnippets = (base, blocks) => {
  let blocksByEl = Hashtbl.create(100);

  let config = Json.parse(Files.readFile(base /+ "bsconfig.json") |! "No bsconfig.json found");

  let deps = [base /+ "lib/bs/src"]; /* TODO find dependency build directories */

  let tmp = base /+ "node_modules/.docre";
  Files.mkdirp(tmp);

  let blockFileName = id => codeBlockPrefix ++ string_of_int(id);

  let blocks = blocks |> List.map(((el, id, fileName, options, content)) => {

    let name = blockFileName(id);
    let re = tmp /+ name ++ ".re";
    let cmt = tmp /+ name ++ ".re_ppx.cmt";
    let js = tmp /+ name ++ ".re_ppx.js";

    /* let cmt = base /+ "lib/bs/src/" ++ name ++ ".cmt";
    let js = base /+ "lib/js/src/" ++ name ++ ".js"; */

    Files.writeFile(re, removeHashes(content) ++ " /* " ++ fileName ++ " */") |> ignore;

    let cmd = refmtCommand(base, re);
    let (output, err, success) = Commands.execFull(cmd);
    let error = if (!success) {
      /* TODO exit hard if it parse fails and you didn't mean to or seomthing. Report this at the end. */
      let out = String.concat("\n", output) ++ String.concat("\n", err);
      let out = Str.global_replace(Str.regexp_string(re), "<snippet>", out);
      if (!options.shouldParseFail) {
        print_endline("Failed to parse " ++ re);
        print_endline(out);
      };
      Some("Parse error:\n" ++ out);
    } else {
      let cmd = justBscCommand(base, re ++ "_ppx.ast", deps);
      let (output, err, success) = Commands.execFull(cmd);
      if (!success) {
        /* TODO exit hard if it parse fails and you didn't mean to or seomthing. Report this at the end. */
        let out = String.concat("\n", output) ++ String.concat("\n", err);
        let out = Str.global_replace(Str.regexp_string(re), "<snippet>", out);
        if (!options.shouldTypeFail) {
          print_endline("Failed to compile " ++ re);
          print_endline(out);
        };
        Some("Compile error:\n" ++ out);
      } else { None };
    };

    Hashtbl.replace(blocksByEl, el, (cmt, js, error));

    (el, id, fileName, options, content, name, js)
  });

  /* let (output, err, success) = Commands.execFull(base /+ "node_modules/.bin/bsb" ++ " -make-world"); */
  /* let (output, err, success) = Commands.execFull(base /+ "node_modules/.bin/bsb" ++ " -make-world -backend js");
  if (!success) {
    print_endline("Bsb output:");
    print_endline(String.concat("\n", output));
    print_endline("Error while running bsb on examples");
  }; */
      /* Unix.unlink(src /+ name ++ ".re"); */

  (blocksByEl, blocks)
};

/* TODO allow package-global settings, like "run this in node" */
let process = (~test, markdowns, cmts, base) =>  {

  let blocks = getCodeBlocks(markdowns, cmts);

  let (blocksByEl, blocks) = compileSnippets(base, blocks);

  if (test) {
    print_endline("Running tests");

    /* TODO run in parallel - maybe all in the same node process?? */
    blocks |> List.iter(((el, id, fileName, options, content, name, js)) => {
      if (test && !options.dontRun) {
        print_endline(string_of_int(id) ++ " - " ++ fileName);
        let (output, err, success) = Commands.execFull("node " ++ js ++ "");
        if (options.shouldRaise) {
          if (success) {
            print_endline(String.concat("\n", output));
            print_endline(String.concat("\n", err));
            print_endline("Expected to fail but didnt " ++ name ++ " in " ++ fileName);
          }
        } else {
          if (!success) {
            print_endline(String.concat("\n", output));
            print_endline(String.concat("\n", err));
            print_endline("Failed to run " ++ name ++ " in " ++ fileName);
          };
        }
      };
      let jsContents = Files.readFile(js);
      ()
    });
  };

  (element) => switch element {
  | Omd.Code_block(lang, content) => {
    switch (Hashtbl.find(blocksByEl, element)) {
    | exception Not_found => {
      print_endline("Not found code block " ++ content);
      None
    }
    | (cmt, js, error) => {
      Some(highlight(lang, content, cmt, js, error))
    }
    }
  }
  | _ => None
  };
};