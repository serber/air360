import Link from "next/link";
import { PortalThemeToggle } from "@/components/PortalThemeToggle";

const navLinks = [
  { href: "/", label: "Overview", id: "home" },
  { href: "/map", label: "Map", id: "map" },
];

const footerGroups = [
  {
    title: "Project",
    links: [
      { href: "/map", label: "Map" },
      { href: "https://github.com/serber/air360", label: "GitHub" },
      { href: "/privacy", label: "Privacy policy" },
    ],
  },
];

export function BrandMark() {
  return (
    <span className="air-brand-mark">
      <span className="air-brand-glyph" aria-hidden="true" />
      <span>air360</span>
    </span>
  );
}

export function ArrowIcon({ className = "air-arrow" }: { className?: string }) {
  return (
    <svg
      aria-hidden="true"
      className={className}
      viewBox="0 0 16 16"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.6"
    >
      <path d="M3 8h10M9 4l4 4-4 4" />
    </svg>
  );
}

export function PortalNav({ active }: { active: string }) {
  return (
    <nav className="air-nav">
      <div className="air-nav-inner">
        <Link href="/" aria-label="Air360">
          <BrandMark />
        </Link>
        <div className="air-nav-links" aria-label="Primary navigation">
          {navLinks.map((link) => (
            <Link
              className={`air-nav-link${link.id === active ? " air-active" : ""}`}
              href={link.href}
              key={link.href}
            >
              {link.label}
            </Link>
          ))}
        </div>
        <div className="air-nav-right">
          <PortalThemeToggle />
        </div>
      </div>
    </nav>
  );
}

export function PortalFooter() {
  return (
    <footer className="air-footer">
      <div className="air-container">
        <div className="air-footer-grid">
          <div>
            <Link href="/" aria-label="Air360">
              <BrandMark />
            </Link>
            <p className="air-muted air-footer-copy">
              An open community network of air-quality sensors with public map
              views and device measurement history.
            </p>
          </div>

          {footerGroups.map((group) => (
            <div key={group.title}>
              <h4>{group.title}</h4>
              <ul>
                {group.links.map((link) => (
                  <li key={`${group.title}-${link.label}`}>
                    <Link href={link.href}>{link.label}</Link>
                  </li>
                ))}
              </ul>
            </div>
          ))}
        </div>
      </div>
    </footer>
  );
}
