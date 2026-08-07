/** Minimal markdown → HTML for updates pages (headings, lists, tables, inline). */

function escapeHtml(value: string): string {
  return String(value || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function inlineMarkdown(text: string): string {
  let html = escapeHtml(text);
  html = html.replace(
    /\[([^\]]+)\]\((https?:\/\/[^)\s]+)\)/g,
    '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>',
  );
  html = html.replace(/`([^`]+)`/g, "<code>$1</code>");
  html = html.replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
  html = html.replace(/(^|[^*])\*([^*]+)\*(?!\*)/g, "$1<em>$2</em>");
  return html;
}

function splitTableRow(row: string): string[] {
  return row
    .replace(/^\|/, "")
    .replace(/\|$/, "")
    .split("|")
    .map((cell) => cell.trim());
}

export function renderMarkdown(source: string): string {
  const lines = String(source || "")
    .replace(/\r\n/g, "\n")
    .split("\n");
  const parts: string[] = [];
  let i = 0;

  while (i < lines.length) {
    const trimmed = lines[i].trim();
    if (!trimmed) {
      i += 1;
      continue;
    }

    const heading = trimmed.match(/^(#{1,3})\s+(.+)$/);
    if (heading) {
      const level = heading[1].length;
      parts.push(
        `<h${level}>${inlineMarkdown(heading[2].trim())}</h${level}>`,
      );
      i += 1;
      continue;
    }

    if (/^[-*]\s+/.test(trimmed)) {
      const items: string[] = [];
      while (i < lines.length && /^[-*]\s+/.test(lines[i].trim())) {
        items.push(
          `<li>${inlineMarkdown(lines[i].trim().replace(/^[-*]\s+/, ""))}</li>`,
        );
        i += 1;
      }
      parts.push(`<ul>${items.join("")}</ul>`);
      continue;
    }

    if (/^\d+\.\s+/.test(trimmed)) {
      const items: string[] = [];
      while (i < lines.length && /^\d+\.\s+/.test(lines[i].trim())) {
        items.push(
          `<li>${inlineMarkdown(lines[i].trim().replace(/^\d+\.\s+/, ""))}</li>`,
        );
        i += 1;
      }
      parts.push(`<ol>${items.join("")}</ol>`);
      continue;
    }

    if (
      trimmed.includes("|") &&
      i + 1 < lines.length &&
      /^\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)+\|?$/.test(lines[i + 1].trim())
    ) {
      const header = splitTableRow(trimmed);
      i += 2;
      const bodyRows: string[][] = [];
      while (i < lines.length && lines[i].trim().includes("|")) {
        bodyRows.push(splitTableRow(lines[i].trim()));
        i += 1;
      }
      const thead = `<thead><tr>${header
        .map((cell) => `<th>${inlineMarkdown(cell)}</th>`)
        .join("")}</tr></thead>`;
      const tbody = `<tbody>${bodyRows
        .map(
          (row) =>
            `<tr>${row
              .map((cell) => `<td>${inlineMarkdown(cell)}</td>`)
              .join("")}</tr>`,
        )
        .join("")}</tbody>`;
      parts.push(
        `<div class="info-table-wrap"><table>${thead}${tbody}</table></div>`,
      );
      continue;
    }

    const para: string[] = [];
    while (i < lines.length) {
      const next = lines[i].trim();
      if (
        !next ||
        /^#{1,3}\s+/.test(next) ||
        /^[-*]\s+/.test(next) ||
        /^\d+\.\s+/.test(next) ||
        (next.includes("|") &&
          i + 1 < lines.length &&
          /^\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)+\|?$/.test(lines[i + 1].trim()))
      ) {
        break;
      }
      para.push(next);
      i += 1;
    }
    const text = para.join(" ").trim();
    if (text) {
      parts.push(`<p>${inlineMarkdown(text)}</p>`);
    }
  }

  return parts.join("");
}

export function formatUpdateDate(iso: string): string {
  const match = String(iso || "")
    .trim()
    .match(/^(\d{4})-(\d{2})-(\d{2})/);
  if (!match) {
    return String(iso || "Update");
  }
  const date = new Date(`${match[1]}-${match[2]}-${match[3]}T12:00:00Z`);
  if (Number.isNaN(date.getTime())) {
    return String(iso);
  }
  try {
    return new Intl.DateTimeFormat("en", {
      year: "numeric",
      month: "long",
      day: "numeric",
    }).format(date);
  } catch {
    return String(iso);
  }
}
