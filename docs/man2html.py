#!/usr/bin/env python3
"""Render the kosh manuals with the Python standard library.

Usage: man2html.py TEMPLATE OUTPUT_DIRECTORY PAGE...
"""

import html
import os
import re
import sys

FONT_MACROS = ("B", "I", "BR", "RB", "BI", "IB", "IR", "RI")


def split_arguments(text):
    return [quoted.replace('""', '"') or word for quoted, word in
            re.findall(r'"((?:[^"]|"")*)"|((?:\\.|[^\s])+)', text)]


def parse_inline(text, font="R"):
    runs = []

    for match in re.finditer(r"\\f([BIR])|\\\((dq|aq)|\\(.)|([^\\]+)", text):
        next_font, quote, escape, plain = match.groups()

        if next_font:
            font = next_font
            continue
        if escape == "&":
            continue

        value = plain or {"dq": '"', "aq": "'"}.get(quote)
        if value is None:
            value = "\\" if escape == "e" else escape
        runs.append((font, html.escape(value, quote=False)))

    return runs


def render_runs(runs):
    merged = []

    for font, text in runs:
        if merged and merged[-1][0] == font:
            merged[-1] = (font, merged[-1][1] + text)
        else:
            merged.append((font, text))

    parts = []

    for font, text in merged:
        tag = {"B": "code", "I": "em"}.get(font)
        parts.append("<%s>%s</%s>" % (tag, text, tag)
                     if tag and text.strip() else text)

    return "".join(parts)


def slug(text):
    """Make an anchor from a heading."""
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


class Page:
    """Convert one man(7) page into an HTML body and its metadata."""

    def __init__(self, source):
        self.source = source
        self.title = ""
        self.section = ""
        self.date = ""
        self.manual = ""
        self.description = ""
        self.out = []
        self.paragraph = []
        self.stack = []
        self.is_tag_pending = False
        self.is_definition_pending = False
        self.preformatted = None
        self.headings = []

    def flush_paragraph(self):
        if self.paragraph:
            text = render_runs(self.paragraph).strip()

            if text:
                self.out.append("<p>" + text + "</p>")

        self.paragraph = []

    def open_definition_if_pending(self):
        if self.is_definition_pending:
            self.out.append("<dd>")
            self.stack.append("dd")
            self.is_definition_pending = False

    def close_to(self, should_stop):
        if self.is_definition_pending:
            self.out.append("<dd></dd>")
            self.is_definition_pending = False

        while self.stack and not should_stop(self.stack[-1]):
            self.close_top()

    def close_top(self):
        kind = self.stack.pop()
        self.out.append({"dd": "</dd>", "dl": "</dl>", "rs": "</div>"}[kind])

    def close_all(self):
        self.flush_paragraph()
        self.close_to(lambda kind: False)

    def add_runs(self, runs):
        if self.is_tag_pending:
            self.is_tag_pending = False
            self.out.append("<dt>" + render_runs(runs).strip() + "</dt>")
            self.is_definition_pending = True
            return

        self.open_definition_if_pending()

        if self.paragraph and runs:
            self.paragraph.append(("R", " "))

        self.paragraph.extend(runs)

    def font_macro_runs(self, fonts, arguments):
        """Render a font macro; one font joins its arguments with blanks."""
        runs = []

        if len(fonts) == 1:
            arguments = [" ".join(arguments)]

        for index, argument in enumerate(arguments):
            runs.extend(parse_inline(argument, fonts[index % len(fonts)]))

        return runs

    def macro(self, name, rest):
        arguments = split_arguments(rest)

        if name == "TH":
            values = arguments + [""] * 5
            self.title, self.section, self.date = values[:3]
            self.manual = values[4] or values[3]
        elif name == "SH" or name == "SS":
            self.close_all()
            text = render_runs(parse_inline(" ".join(arguments)))
            anchor = slug(html.unescape(re.sub("<[^>]+>", "", text)))
            level = 2 if name == "SH" else 3
            self.headings.append((level, anchor, text))
            self.out.append('<h%d id="%s">%s</h%d>'
                            % (level, anchor, text, level))
        elif name == "PP":
            self.flush_paragraph()
            self.close_to(lambda kind: kind == "rs")
        elif name == "TP":
            self.flush_paragraph()

            if self.is_definition_pending:
                self.is_tag_pending = True
                return

            if self.stack and self.stack[-1] == "dd":
                self.close_top()

            if not self.stack or self.stack[-1] != "dl":
                self.out.append("<dl>")
                self.stack.append("dl")

            self.is_tag_pending = True
        elif name == "IP":
            self.flush_paragraph()
            is_in_list = self.is_definition_pending or (
                self.stack and self.stack[-1] in ("dd", "dl"))

            if is_in_list:
                self.open_definition_if_pending()

                if self.stack[-1] == "dl":
                    self.out.append("<dd>")
                    self.stack.append("dd")
            else:
                self.out.append('<div class="rs">')
                self.stack.append("rs")
        elif name == "RS":
            self.flush_paragraph()
            self.open_definition_if_pending()
            self.out.append('<div class="rs">')
            self.stack.append("rs")
        elif name == "RE":
            self.flush_paragraph()
            self.close_to(lambda kind: kind == "rs")

            if self.stack:
                self.close_top()
        elif name in ("nf", "EX"):
            self.flush_paragraph()
            self.open_definition_if_pending()
            self.preformatted = []
        elif name == "br":
            if self.paragraph:
                self.paragraph.append(("R", "<br>"))
        elif name in FONT_MACROS:
            self.add_runs(self.font_macro_runs(name, arguments))

    def preformatted_line(self, line):
        if line.startswith("."):
            name, _, rest = line[1:].strip().partition(" ")

            if name in ("fi", "EE"):
                body = "\n".join(self.preformatted)
                self.out.append("<pre>" + body + "</pre>")
                self.preformatted = None
            elif name in FONT_MACROS:
                runs = self.font_macro_runs(name, split_arguments(rest))
                self.preformatted.append(render_runs(runs))

            return

        self.preformatted.append(render_runs(parse_inline(line)))

    def convert(self):
        for line in self.source.splitlines():
            if self.preformatted is not None:
                self.preformatted_line(line)
                continue

            if line.startswith("."):
                name, _, rest = line[1:].strip().partition(" ")

                if name:
                    self.macro(name, rest)

                continue

            if not line.strip():
                self.flush_paragraph()
                continue

            self.add_runs(parse_inline(line))

        self.close_all()
        self.description = self.find_description()

    def find_description(self):
        text = "".join(self.out)
        match = re.search(r'<h2 id="name">.*?</h2><p>(.*?)</p>', text)

        if not match:
            return ""

        return re.sub(r"\s+", " ", re.sub("<[^>]+>", "", match.group(1)))


def link_references(body, pages, headings):
    """Link page references to sibling pages and section titles to anchors."""
    for page in pages:
        title, section = page
        pattern = r"<code>%s</code>\(%s\)" % (re.escape(title), section)
        target = '<a href="%s.%s.html"><code>%s</code>(%s)</a>' % (
            title, section, title, section)
        body = re.sub(pattern, target, body)

    for level, anchor, text in headings:
        if level != 2 or anchor == "name":
            continue

        pattern = r"<code>%s</code>" % re.escape(text)
        target = '<a href="#%s"><code>%s</code></a>' % (anchor, text)
        body = re.sub(pattern, target, body)

    return body


def table_of_contents(headings):
    items = []
    is_sublist_open = False

    for level, anchor, text in headings:
        link = '<a href="#%s">%s</a>' % (anchor, text)

        if level == 2:
            if is_sublist_open:
                items.append("</ul></li>")
                is_sublist_open = False
            elif items:
                items.append("</li>")
            items.append("<li>" + link)
        else:
            if not is_sublist_open:
                items.append("<ul>")
                is_sublist_open = True
            items.append("<li>" + link + "</li>")

    items.append("</ul></li>" if is_sublist_open else "</li>")
    return "<ul>" + "".join(items) + "</ul>"


def main(argv):
    if len(argv) < 4:
        sys.stderr.write("usage: man2html.py TEMPLATE OUTPUT_DIRECTORY "
                         "PAGE...\n")
        return 2

    template_path, output_directory, page_paths = argv[1], argv[2], argv[3:]

    with open(template_path, encoding="utf-8") as stream:
        template = stream.read()

    pages = []

    for path in page_paths:
        with open(path, encoding="utf-8") as stream:
            page = Page(stream.read())

        page.convert()
        pages.append((path, page))

    references = [(page.title.lower(), page.section) for _, page in pages]
    os.makedirs(output_directory, exist_ok=True)

    for path, page in pages:
        name = "%s(%s)" % (page.title.lower(), page.section)
        body = link_references("\n".join(page.out), references, page.headings)
        values = {
            "title": html.escape(name),
            "description": html.escape(page.description),
            "manual": html.escape(page.manual),
            "date": html.escape(page.date),
            "toc": table_of_contents(page.headings),
            "content": body,
        }
        document = re.sub(r"\{\{(\w+)\}\}",
                          lambda match: values.get(match.group(1), ""),
                          template)
        output = os.path.join(output_directory,
                              os.path.basename(path) + ".html")

        with open(output, "w", encoding="utf-8") as stream:
            stream.write(document)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
