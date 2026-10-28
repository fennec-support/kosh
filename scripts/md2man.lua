--
--    This file is a part of the Koshka shell, (c) toiletbril, 2026
--    See the top-level LICENSE file for the licensing information.
--
-- A pandoc filter that turns a manual in docs/ into a man page. The first
-- heading is the title, a block quote after it becomes the NAME section, and a
-- paragraph holding only a date becomes the page date. Links within the
-- manuals keep only their text.

local MONTH_NAMES = {
  "January", "February", "March", "April", "May", "June", "July", "August",
  "September", "October", "November", "December",
}

local function is_external(target)
  return target:match("^%a[%w+.-]*:") ~= nil
end

local function upper_inlines(inlines)
  return pandoc.Inlines(inlines):walk({
    Str = function(text)
      return pandoc.Str(pandoc.text.upper(text.text))
    end,
  })
end

function Link(link)
  if is_external(link.target) then return nil end

  return link.content
end

function RawInline(raw)
  if raw.format == "html" then return {} end
end

function RawBlock(raw)
  if raw.format == "html" then return {} end
end

function Pandoc(doc)
  local program = pandoc.utils.stringify(doc.meta.name or "kosh")
  local blocks  = pandoc.Blocks({})
  local index   = 1

  local first = doc.blocks[index]
  if first and first.t == "Header" and first.level == 1 then
    index = index + 1
  end

  local quote = doc.blocks[index]
  if quote and quote.t == "BlockQuote" then
    local summary = pandoc.utils.stringify(quote):gsub("%.$", "")

    summary = pandoc.text.lower(pandoc.text.sub(summary, 1, 1))
        .. pandoc.text.sub(summary, 2)
    blocks:insert(pandoc.Header(1, "NAME"))
    blocks:insert(pandoc.Para(program .. " - " .. summary))
    index = index + 1
  end

  local date = doc.blocks[index]
  if date and date.t == "Para" then
    local year, month, day =
        pandoc.utils.stringify(date):match("^(%d%d%d%d)%-(%d%d)%-(%d%d)$")

    -- man reads "October 26, 2026" without the escapes of a hyphenated date.
    if year then
      doc.meta.date = string.format("%s %d, %s", MONTH_NAMES[tonumber(month)],
                                    tonumber(day), year)
      index = index + 1
    end
  end

  for position = index, #doc.blocks do
    local block = doc.blocks[position]

    if block.t == "Header" then
      local level   = block.level > 1 and block.level - 1 or 1
      local content = pandoc.Inlines(block.content)

      -- the anchor after a heading leaves a space behind.
      while #content > 0 and content[#content].t == "Space" do
        content:remove(#content)
      end

      if level == 1 then content = upper_inlines(content) end

      block = pandoc.Header(level, content, block.attr)
    end

    blocks:insert(block)
  end

  doc.blocks = blocks

  return doc
end
