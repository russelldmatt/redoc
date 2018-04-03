
open Parsetree;

/*
 * TODO: figure out what things can be linked.
 * e.g. what modules expose what items.
 *
 * For linking to other things, I'm not sure what I can do.
 *
 * hmmm so I want "include" to show me things that link as well.
 * Look at Reprocessing_Types, Reprocessing.rei too
 *
 * Also referencing the things from another module.
 * Like `module Draw = ...`, have the contents there w/ IDs (& types?). (won't have docs, but thats ok)
 */

let generate = (name, input) => {
  let (stampsToPaths, (toplevel, allDocs)) = switch input {
  | `Structure(structure, ast) => {
    let (sp, tl) = PrepareDocs.organizeTypes((name, []), structure.Typedtree.str_items);
    Printast.implementation(Format.str_formatter, ast);
    let out = Format.flush_str_formatter();
    Files.writeFile("./ast.impl", out) |> ignore;
    (sp, PrepareDocs.findAllDocs(ast, tl))
  }
  | `Signature(signature, ast) => {
    Printast.interface(Format.str_formatter, ast);
    let out = Format.flush_str_formatter();
    Files.writeFile("./ast.inft", out) |> ignore;
    let (sp, tl) = PrepareDocs.organizeTypesIntf((name, []), signature.Typedtree.sig_items);
    (sp, PrepareDocs.findAllDocsIntf(ast, tl))
  }
  };

  let mainMarkdown = switch (toplevel) {
  | None => GenerateDoc.defaultMain(name)
  | Some(doc) => doc
  };

  let formatHref = ((modName, inner, ptype)) => {
    let modName = if (modName == "<global>") {
      "globals"
    } else {
      modName
    };
    let hash = "#" ++ GenerateDoc.makeId(inner, ptype);
    if (modName == name) {
      hash
    } else {
      modName ++ ".html" ++ hash
    }
  };

  GenerateDoc.head(name) ++
  GenerateDoc.docsForModule(formatHref, stampsToPaths, [], name, mainMarkdown, allDocs)
};