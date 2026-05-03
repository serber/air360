import Link from "next/link";

const portalSections = [
  {
    title: "Device map",
    description: "Public map with Air360 devices and their latest readings.",
    href: "/map",
    cta: "Open map",
  },
  {
    title: "Build a device",
    description: "Assembly guide placeholder for future hardware instructions.",
    href: "/build",
    cta: "Open guide",
  },
];

export default function Home() {
  return (
    <main className="min-h-screen bg-[#f4f7f5] px-4 py-8 text-slate-950 sm:px-6 lg:px-8">
      <div className="mx-auto flex w-full max-w-6xl flex-col gap-8">
        <section className="rounded-md border border-slate-200 bg-white p-8 shadow-sm">
          <p className="text-xs font-semibold uppercase tracking-[0.16em] text-emerald-700">
            Air360
          </p>
          <h1 className="mt-3 text-3xl font-semibold tracking-tight sm:text-4xl">
            Public Air360 portal
          </h1>
          <p className="mt-4 max-w-2xl text-base leading-7 text-slate-600">
            This home page is a placeholder for the future public entry point.
          </p>
        </section>

        <section className="grid gap-4 md:grid-cols-2" aria-label="Portal pages">
          {portalSections.map((section) => (
            <article
              className="rounded-md border border-slate-200 bg-white p-6 shadow-sm"
              key={section.href}
            >
              <h2 className="text-xl font-semibold text-slate-950">
                {section.title}
              </h2>
              <p className="mt-3 text-sm leading-6 text-slate-600">
                {section.description}
              </p>
              <Link
                className="mt-5 inline-flex rounded-md bg-slate-950 px-4 py-2 text-sm font-semibold text-white transition hover:bg-slate-800"
                href={section.href}
              >
                {section.cta}
              </Link>
            </article>
          ))}
        </section>

        <footer className="text-center text-xs text-slate-400">
          <Link href="/privacy" className="hover:underline">
            Privacy Policy
          </Link>
        </footer>
      </div>
    </main>
  );
}
