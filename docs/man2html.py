#!/usr/bin/env python3
"""Convert the kosh man(7) pages into HTML pages through one template.

Usage: man2html.py TEMPLATE OUTPUT_DIRECTORY PAGE...

Each PAGE, such as docs/kosh.1, becomes OUTPUT_DIRECTORY/kosh.1.html. The
converter handles the man(7) macros and escapes that the kosh pages use and
needs nothing beyond the Python standard library.
"""

import html
import os
import re
import sys

SPECIAL_CHARACTERS = {
    "dq": '"',
    "aq": "'",
    "rs": "\\",
    "em": "&mdash;",
    "en": "&ndash;",
    "bu": "&bull;",
    "lq": "&ldquo;",
    "rq": "&rdquo;",
    "oq": "&lsquo;",
    "cq": "&rsquo;",
    "co": "&copy;",
    "mu": "&times;",
    "ti": "~",
    "ha": "^",
    "ga": "`",
}

FONT_MACROS = {
    "B": ("B",),
    "I": ("I",),
    "SB": ("B",),
    "SM": ("R",),
    "BR": ("B", "R"),
    "RB": ("R", "B"),
    "BI": ("B", "I"),
    "IB": ("I", "B"),
    "IR": ("I", "R"),
    "RI": ("R", "I"),
}

FONT_ALIASES = {
    "R": "R",
    "P": "P",
    "B": "B",
    "I": "I",
    "C": "C",
    "CR": "C",
    "CW": "C",
    "CB": "B",
    "CI": "I",
    "BI": "B",
    "1": "R",
    "2": "I",
    "3": "B",
}


def split_arguments(text):
    """Split macro arguments on blanks, honoring double quotes."""
    arguments = []
    index = 0
    length = len(text)

    while index < length:
        while index < length and text[index] in " \t":
            index += 1

        if index >= length:
            break

        if text[index] == '"':
            index += 1
            argument = []

            while index < length:
                if text[index] == '"':
                    if index + 1 < length and text[index + 1] == '"':
                        argument.append('"')
                        index += 2
                        continue

                    index += 1
                    break

                argument.append(text[index])
                index += 1

            arguments.append("".join(argument))
        else:
            start = index

            while index < length and text[index] not in " \t":
                if text[index] == "\\" and index + 1 < length:
                    index += 1
                index += 1

            arguments.append(text[start:index])

    return arguments


def strip_comment(line):
    """Remove a roff comment that starts with an escaped double quote."""
    index = 0

    while index < len(line):
        if line[index] == "\\" and index + 1 < len(line):
            if line[index + 1] == '"':
                return line[:index].rstrip()
            index += 2
            continue
        index += 1

    return line


class InlineState:
    """Track the current and previous font across inline text."""

    def __init__(self, font="R"):
        self.font = font
        self.previous_font = font

    def set_font(self, font):
        if font == "P":
            self.font, self.previous_font = self.previous_font, self.font
        else:
            self.previous_font = self.font
            self.font = font


def parse_inline(text, state):
    """Turn roff text into (font, html) runs, updating the font state."""
    runs = []
    buffer = []

    def flush():
        if buffer:
            runs.append((state.font, "".join(buffer)))
            buffer.clear()

    index = 0
    length = len(text)

    while index < length:
        character = text[index]

        if character != "\\":
            buffer.append(html.escape(character, quote=False))
            index += 1
            continue

        index += 1

        if index >= length:
            break

        escape = text[index]
        index += 1

        if escape == "f":
            if index < length and text[index] == "(":
                font = text[index + 1:index + 3]
                index += 3
            elif index < length and text[index] == "[":
                end = text.find("]", index)
                font = text[index + 1:end]
                index = end + 1
            else:
                font = text[index:index + 1]
                index += 1

            flush()
            state.set_font(FONT_ALIASES.get(font, "R"))
        elif escape == "(":
            name = text[index:index + 2]
            index += 2
            buffer.append(SPECIAL_CHARACTERS.get(name, ""))
        elif escape == "[":
            end = text.find("]", index)
            name = text[index:end]
            index = end + 1
            buffer.append(SPECIAL_CHARACTERS.get(name, ""))
        elif escape == "*":
            if index < length and text[index] == "(":
                index += 3
            elif index < length and text[index] == "[":
                index = text.find("]", index) + 1
            else:
                index += 1
        elif escape in "e\\":
            buffer.append("\\")
        elif escape in "-":
            buffer.append("-")
        elif escape in " ~0":
            buffer.append("&nbsp;")
        elif escape in "&|^%:c":
            pass
        elif escape == ".":
            buffer.append(".")
        elif escape == "'":
            buffer.append("'")
        elif escape == "`":
            buffer.append("`")
        else:
            buffer.append(html.escape(escape, quote=False))

    flush()
    return runs


def render_runs(runs):
    """Render (font, html) runs, merging neighbors that share a font."""
    merged = []

    for font, text in runs:
        if font == "C":
            font = "B"

        if merged and merged[-1][0] == font:
            merged[-1] = (font, merged[-1][1] + text)
        else:
            merged.append((font, text))

    parts = []

    for font, text in merged:
        if font == "B" and text.strip():
            parts.append("<code>" + text + "</code>")
        elif font == "I" and text.strip():
            parts.append("<em>" + text + "</em>")
        else:
            parts.append(text)

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
        self.next_line_font = None
        self.headings = []
        self.state = InlineState()

    # Containers.

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

    # Text.

    def add_runs(self, runs):
        if self.is_tag_pending:
            self.is_tag_pending = False
            self.out.append("<dt>" + render_runs(runs).strip() + "</dt>")
            self.is_definition_pending = True
            return

        self.open_definition_if_pending()

        if self.paragraph and runs:
            self.paragraph.append((self.state.font, " "))

        self.paragraph.extend(runs)

    def add_text_line(self, line):
        if self.next_line_font is not None:
            local = InlineState(self.next_line_font)
            self.next_line_font = None
            self.add_runs(parse_inline(line, local))
        else:
            self.add_runs(parse_inline(line, self.state))

    def font_macro_runs(self, fonts, arguments):
        """Render a font macro; one font joins its arguments with blanks."""
        runs = []

        if len(fonts) == 1:
            arguments = [" ".join(arguments)]

        for index, argument in enumerate(arguments):
            local = InlineState(fonts[index % len(fonts)])
            runs.extend(parse_inline(argument, local))

        return runs

    # Macros.

    def macro(self, name, rest):
        arguments = split_arguments(rest)

        if name == "TH":
            values = arguments + [""] * 5
            self.title, self.section, self.date = values[:3]
            self.manual = values[4] or values[3]
        elif name == "SH" or name == "SS":
            self.close_all()
            text = render_runs(parse_inline(" ".join(arguments),
                                            InlineState()))
            anchor = slug(html.unescape(re.sub("<[^>]+>", "", text)))
            level = 2 if name == "SH" else 3
            self.headings.append((level, anchor, text))
            self.out.append('<h%d id="%s">%s</h%d>'
                            % (level, anchor, text, level))
        elif name in ("PP", "LP", "P"):
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

            if arguments:
                if not is_in_list:
                    self.out.append("<dl>")
                    self.stack.append("dl")
                elif self.stack and self.stack[-1] == "dd":
                    self.close_top()

                self.is_tag_pending = True
                self.add_runs(parse_inline(arguments[0], InlineState()))
            elif is_in_list:
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
        elif name == "sp":
            self.flush_paragraph()
        elif name in FONT_MACROS:
            if arguments:
                self.add_runs(self.font_macro_runs(FONT_MACROS[name],
                                                   arguments))
            else:
                self.next_line_font = FONT_MACROS[name][0]
        elif name == "ft":
            font = arguments[0] if arguments else "P"
            self.state.set_font(FONT_ALIASES.get(font, "R"))

    def preformatted_line(self, line):
        if line.startswith(".") or line.startswith("'"):
            name, _, rest = line[1:].strip().partition(" ")

            if name in ("fi", "EE"):
                body = "\n".join(self.preformatted)
                self.out.append("<pre>" + body + "</pre>")
                self.preformatted = None
            elif name == "ft":
                font = rest.strip() or "P"
                self.state.set_font(FONT_ALIASES.get(font, "R"))
            elif name in FONT_MACROS:
                runs = self.font_macro_runs(FONT_MACROS[name],
                                            split_arguments(rest))
                self.preformatted.append(render_runs(runs))

            return

        state = InlineState()
        self.preformatted.append(render_runs(parse_inline(line, state)))

    def convert(self):
        for raw in self.source.splitlines():
            if raw.startswith(".\\\"") or raw.startswith("'\\\""):
                continue

            line = strip_comment(raw)

            if self.preformatted is not None:
                self.preformatted_line(line)
                continue

            if line.startswith(".") or line.startswith("'"):
                name, _, rest = line[1:].strip().partition(" ")

                if name:
                    self.macro(name, rest)

                continue

            if not line.strip():
                self.flush_paragraph()
                continue

            self.add_text_line(line)

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
            "header": html.escape("%s(%s)" % (page.title, page.section)),
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
