# HTML rendering rules for list items

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `1af0f870-efa6-4be2-b872-44b08130c90e`  
**Created:** 2026-06-03  
**Messages:** 4 (2 prompts)

**Related commits:**

- `8722ae1 fix loose lists handling`
- `817995f fix list item HTML rendering`
- `7722ba9 fix lists items html rendering`

---

## ▸ Prompt — 2026-06-03 23:11

> Which are the html render rules for lists items?

# HTML List Item Render Rules

Here are the key rendering rules for list items in HTML:

## Ordered Lists (`<ol>`)
- **Display**: Block-level by default
- **Numbering**: Automatically generates numbers (1, 2, 3...) unless styled otherwise
- **Margin**: Default top/bottom margins (typically 16px)
- **Padding**: Default left padding for the number (typically 40px)
- **Number format**: Controlled by the `type` attribute
  - `type="1"` → 1, 2, 3 (default)
  - `type="a"` → a, b, c
  - `type="A"` → A, B, C
  - `type="i"` → i, ii, iii
  - `type="I"` → I, II, III

## Unordered Lists (`<ul>`)
- **Display**: Block-level by default
- **Bullets**: Renders as disc (●), circle (○), or square (■)
- **Margin**: Default top/bottom margins (typically 16px)
- **Padding**: Default left padding for bullets (typically 40px)
- **Bullet type**: Controlled by the `list-style-type` CSS property or `type` attribute (legacy)

## List Items (`<li>`)
- **Display**: Block-level by default (when inside `<ul>` or `<ol>`)
- **Marker**: Automatically positioned outside the content box (left side by default)
- **Marker placement**: Controlled by `list-style-position: inside | outside`
  - `outside` (default): marker rendered outside the content box
  - `inside`: marker flows with the text content
- **Inherits**: Font properties from parent list and document
- **Padding/Margin**: No default padding; margins can be inherited or set explicitly

## Key CSS Properties
- `list-style-type` → bullet or number style
- `list-style-position` → marker placement
- `list-style-image` → custom bullet image
- `list-style` → shorthand for all three above
- `marker-offset` → distance between marker and content (limited browser support)

## Nesting
- Lists can be nested; inner lists inherit the parent's `list-style-type` by default
- Nested lists should be placed **inside the parent `<li>`** element, not as siblings

These are the standard rendering behaviors across modern browsers, though some properties have limited or inconsistent support.

## ▸ Prompt — 2026-06-03 23:11

> When do I need to append \n after <li>?

You **don't need to append `\n` after `<li>`** in HTML.

Here's why:

## Whitespace in HTML
- HTML treats most whitespace (spaces, tabs, newlines) as a **single space** or **collapses it entirely** depending on context
- Newlines after `<li>` tags are **ignored by the browser** during rendering
- They have **no effect on the visual output**

## When to use `\n` after `<li>`
You only need newlines for **readability in your source code**, not for functionality:

```html
<!-- Readable (with newlines) -->
<ul>
  <li>Item 1</li>
  <li>Item 2</li>
  <li>Item 3</li>
</ul>

<!-- Also works fine (no newlines) -->
<ul>
  <li>Item 1</li><li>Item 2</li><li>Item 3</li>
</ul>
```

Both render identically.

## Exception: Text content
If you have **text content immediately after `</li>`**, a newline won't hurt but also won't create a visible space (unless it's inside a `<pre>` tag):

```html
<li>Item</li>
Next text here  <!-- rendered on the same line -->
```

## Best Practice
- Use newlines and indentation **for code readability** during development
- They make your HTML easier to maintain and debug
- Minifiers will remove them anyway if you're optimizing for production

**TL;DR**: Append `\n` for clean, readable code—not for HTML rendering.

