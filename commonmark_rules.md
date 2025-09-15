# CommonMark Rendering Rules (for Coding LLMs & Copilot integration)

## 1. Supported Markdown Elements

You MUST support the following CommonMark elements:

- **Headings:**  
  `# H1`, `## H2`, ..., up to `###### H6`
- **Paragraphs:**  
  Text separated by one or more blank lines.
- **Emphasis:**  
  - Italic: `*italic*` or `_italic_`
  - Bold: `**bold**` or `__bold__`
  - Bold+italic: `***bold italic***`
- **Blockquotes:**  
  `> quoted text`
- **Lists:**  
  - Unordered: `- item`, `* item`, or `+ item`
  - Ordered: `1. item`, `2. item`
- **Code:**  
  - Inline code: `` `code` ``
  - Code block:  
    - Indent by 4 spaces or 1 tab, **or**  
    - Triple backticks:  
      ```
      ```
      code block
      ```
      ```
- **Links:**  
  `[text](url)` and autolinks: `<https://example.com>`
- **Images:**  
  `![alt text](url)`
- **Horizontal rule:**  
  `---`, `***`, or `___` on a line by itself.
- **Line breaks:**  
  See rules below.
- **HTML blocks or inline HTML:**  
  Raw HTML is passed through as-is.

**You SHOULD NOT implement features not listed above.**

---

## 2. Line Break Rules

**All line break handling MUST follow these rules:**

- **Paragraph separation:**  
  A blank line (two consecutive newlines) always starts a new paragraph.
- **Soft line break (single newline):**  
  A single newline in the source Markdown (not followed by a blank line) MUST be rendered as a space in output (NOT a visible line break).  
  _Example:_  
  ```
  this is line 1
  this is line 2
  ```
  → rendered as:  
  this is line 1 this is line 2

- **Hard line break:**  
  Only insert a visible line break (`<br>` or equivalent) in these cases:
    - The line ends with **two or more spaces**, then a newline.
    - The line ends with a backslash `\`, then a newline.

  _Example (two spaces at end):_
  ```
  line one␣␣
  line two
  ```
  → rendered as:
  line one  
  line two

  _Example (backslash at end):_
  ```
  line one\
  line two
  ```
  → rendered as:
  line one  
  line two

- **Code blocks and code spans:**  
  Preserve all line breaks and whitespace literally inside code blocks (indented or fenced) and inline code spans.

- **List items:**  
  Each new list item starts on a new line.

- **Block elements:**  
  Block elements (like headings, blockquotes, lists, code blocks, thematic breaks) MUST be separated by at least one blank line, except where CommonMark allows tight lists (see below).

---

## 3. Special notes for implementers

- Do NOT insert extra blank lines between block elements unless explicitly present in the source.
- Do NOT collapse or ignore blank lines inside code blocks.
- Tight lists:  
  - For lists without blank lines between items, render as “tight” (no `<p>`).
  - For lists with blank lines between items, render as “loose” (wrap each item in `<p>`).

- Render all whitespace and breaks inside code blocks and code spans EXACTLY as in the source.

---

## 4. References

- [CommonMark Spec: Line Breaks](https://spec.commonmark.org/0.30/#hard-line-breaks)
- [CommonMark Spec: List of Elements](https://spec.commonmark.org/0.30/#blocks-and-inlines)
- [CommonMark Interactive Dingus](https://spec.commonmark.org/dingus/)

---

## 5. Error handling

If the LLM is unsure about whether a line break should be visible, it MUST point to the relevant rule above and/or ask the user for clarification.
