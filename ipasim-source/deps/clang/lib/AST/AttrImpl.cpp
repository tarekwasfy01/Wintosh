//===--- AttrImpl.cpp - Classes for representing attributes -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file contains out-of-line methods for Attr classes.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
// [port] CHANGED: Added this `#include`. Required by function
// [port] `printExprAsWritten`.
#include "clang/Lex/Lexer.h"
using namespace clang;

// [port] CHANGED: Added this function. See [pretty-print]. It was copied from
// [port] `StmtPrinter.cpp`.
/// Prints the given expression using the original source text. Returns true on
/// success, false otherwise.
static bool printExprAsWritten(raw_ostream &OS, Expr *E,
                               const ASTContext *Context) {
  if (!Context)
    return false;
  bool Invalid = false;
  StringRef Source = Lexer::getSourceText(
      CharSourceRange::getTokenRange(E->getSourceRange()),
      Context->getSourceManager(), Context->getLangOpts(), &Invalid);
  if (!Invalid) {
    OS << Source;
    return true;
  }
  return false;
}

#include "clang/AST/AttrImpl.inc"
