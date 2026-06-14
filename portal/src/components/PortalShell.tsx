import Link from "next/link";
import { useTranslations } from "next-intl";
import { PortalLocaleToggle } from "@/components/PortalLocaleToggle";
import { PortalThemeToggle } from "@/components/PortalThemeToggle";

const navLinks = [
  { href: "/", labelKey: "overview", id: "home" },
  { href: "/map", labelKey: "map", id: "map" },
  { href: "/build", labelKey: "build", id: "build" },
];

const footerGroups = [
  {
    titleKey: "project",
    links: [
      { href: "/map", labelKey: "map", namespace: "common" },
      { href: "/build", labelKey: "buildGuide", namespace: "common" },
      { href: "https://github.com/serber/air360", labelKey: "github", namespace: "common" },
      { href: "/privacy", labelKey: "privacyPolicy", namespace: "common" },
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
  const common = useTranslations("common");
  const nav = useTranslations("nav");

  return (
    <nav className="air-nav">
      <div className="air-nav-inner">
        <Link href="/" aria-label={common("brandAria")}>
          <BrandMark />
        </Link>
        <div className="air-nav-links" aria-label={common("primaryNavigation")}>
          {navLinks.map((link) => (
            <Link
              className={`air-nav-link${link.id === active ? " air-active" : ""}`}
              href={link.href}
              key={link.href}
            >
              {nav(link.labelKey)}
            </Link>
          ))}
        </div>
        <div className="air-nav-right">
          <PortalLocaleToggle />
          <PortalThemeToggle />
        </div>
      </div>
    </nav>
  );
}

export function PortalFooter() {
  const common = useTranslations("common");
  const footer = useTranslations("footer");

  return (
    <footer className="air-footer">
      <div className="air-container">
        <div className="air-footer-grid">
          <div>
            <Link href="/" aria-label={common("brandAria")}>
              <BrandMark />
            </Link>
            <p className="air-muted air-footer-copy">
              {footer("copy")}
            </p>
          </div>

          {footerGroups.map((group) => (
            <div key={group.titleKey}>
              <h4>{footer(group.titleKey)}</h4>
              <ul>
                {group.links.map((link) => (
                  <li key={`${group.titleKey}-${link.href}`}>
                    <Link href={link.href}>
                      {link.namespace === "common"
                        ? common(link.labelKey)
                        : footer(link.labelKey)}
                    </Link>
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
