# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import re
import shlex


def parse_expr(expr_text, valid_variables):
    """ parses the simple criteria expression syntax used in
    dependency specifications.
    Returns an ExprNode instance that can be evaluated like this:

    ```
    expr = parse_expr("os=windows")
    ok = expr.eval({
        "os": "windows"
    })
    ```

    Whitespace is allowed between tokens.  The following terms
    are recognized:

    KEY = VALUE   # Evaluates to True if ctx[KEY] == VALUE
    not(EXPR)     # Evaluates to True if EXPR evaluates to False
                  # and vice versa
    all(EXPR1, EXPR2, ...) # Evaluates True if all of the supplied
                           # EXPR's also evaluate True
    any(EXPR1, EXPR2, ...) # Evaluates True if any of the supplied
                           # EXPR's also evaluate True, False if
                           # none of them evaluated true.
    """

    p = Parser(expr_text, valid_variables)
    return p.parse()


class ExprNode(object):
    def eval(self, ctx):
        return False


class TrueExpr(ExprNode):
    def eval(self, ctx):
        return True

    def __str__(self):
        return "true"


class NotExpr(ExprNode):
    def __init__(self, node):
        self._node = node

    def eval(self, ctx):
        return not self._node.eval(ctx)

    def __str__(self):
        return "not(%s)" % self._node


class AllExpr(ExprNode):
    def __init__(self, nodes):
        self._nodes = nodes

    def eval(self, ctx):
        for node in self._nodes:
            if not node.eval(ctx):
                return False
        return True

    def __str__(self):
        items = []
        for node in self._nodes:
            items.append(str(node))
        return "all(%s)" % ",".join(items)


class AnyExpr(ExprNode):
    def __init__(self, nodes):
        self._nodes = nodes

    def eval(self, ctx):
        for node in self._nodes:
            if node.eval(ctx):
                return True
        return False

    def __str__(self):
        items = []
        for node in self._nodes:
            items.append(str(node))
        return "any(%s)" % ",".join(items)


class EqualExpr(ExprNode):
    def __init__(self, key, value):
        self._key = key
        self._value = value

    def eval(self, ctx):
        return ctx.get(self._key) == self._value

    def __str__(self):
        return "%s=%s" % (self._key, self._value)


class Parser(object):
    def __init__(self, text, valid_variables):
        self.text = text
        self.lex = shlex.shlex(text)
        self.valid_variables = valid_variables

    def parse(self):
        expr = self.top()
        garbage = self.lex.get_token()
        if garbage != "":
            raise Exception(
                "Unexpected token %s after EqualExpr in %s" % (garbage, self.text)
            )
        return expr

    def top(self):
        name = self.ident()
        op = self.lex.get_token()

        if op == "(":
            parsers = {
                "not": self.parse_not,
                "any": self.parse_any,
                "all": self.parse_all,
            }
            func = parsers.get(name)
            if not func:
                raise Exception("invalid term %s in %s" % (name, self.text))
            return func()

        if op == "=":
            if name not in self.valid_variables:
                raise Exception("unknown variable %r in expression" % (name,))
            return EqualExpr(name, self.lex.get_token())

        raise Exception(
            "Unexpected token sequence '%s %s' in %s" % (name, op, self.text)
        )

    def ident(self):
        ident = self.lex.get_token()
        if not re.match("[a-zA-Z]+", ident):
            raise Exception("expected identifier found %s" % ident)
        return ident

    def parse_not(self):
        node = self.top()
        expr = NotExpr(node)
        tok = self.lex.get_token()
        if tok != ")":
            raise Exception("expected ')' found %s" % tok)
        return expr

    def parse_any(self):
        nodes = []
        while True:
            nodes.append(self.top())
            tok = self.lex.get_token()
            if tok == ")":
                break
            if tok != ",":
                raise Exception("expected ',' or ')' but found %s" % tok)
        return AnyExpr(nodes)

    def parse_all(self):
        nodes = []
        while True:
            nodes.append(self.top())
            tok = self.lex.get_token()
            if tok == ")":
                break
            if tok != ",":
                raise Exception("expected ',' or ')' but found %s" % tok)
        return AllExpr(nodes)
