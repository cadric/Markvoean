# Setext Headings Test

Test various heading formats to verify Setext support.

## ATX Headings (should work)

# H1 ATX
## H2 ATX
### H3 ATX

## Setext Headings (need to test)

# H1 Setext
## H2 Setext
# Another H1
## Another H2
# Mixed content

This should be an H1 with **bold** and *italic* text.

## Mixed H2

This should be H2 with `code` and [link](http://example.com).

## Edge Cases

# Short
# Very long heading with lots of text that goes on and on
## Multi
## word
## heading

No underline below

This is just a paragraph.

# Wrong underline count

Should be paragraph, not heading.

# Correct underline

This should work as H1.
