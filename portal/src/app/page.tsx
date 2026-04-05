export default function Home() {
  const portalAreas = [
    {
      title: "Public landing",
      description:
        "A public-facing entry point for Air360 with product context, navigation, and onboarding paths.",
    },
    {
      title: "Account area",
      description:
        "A reserved area for registration, sign-in, personal account screens, and device management workflows.",
    },
  ];

  const projectStack = [
    "Next.js 16 App Router",
    "React 19",
    "TypeScript",
    "Tailwind CSS 4",
    "ESLint 9",
  ];

  return (
    <div className="flex min-h-screen flex-col bg-[radial-gradient(circle_at_top,_rgba(106,227,201,0.22),_transparent_34%),linear-gradient(180deg,_#f6fcfa_0%,_#edf5f2_46%,_#e3ece9_100%)]">
      <main className="mx-auto flex w-full max-w-6xl flex-1 flex-col px-6 pb-16 pt-10 sm:px-10 lg:px-12">
        <section className="grid gap-8 lg:grid-cols-[minmax(0,1.35fr)_minmax(320px,0.85fr)] lg:items-start">
          <div className="rounded-[2rem] border border-black/8 bg-white/82 p-8 shadow-[0_24px_80px_rgba(17,24,39,0.08)] backdrop-blur md:p-10">
            <p className="text-sm font-semibold uppercase tracking-[0.28em] text-emerald-800/75">
              Air360 Portal
            </p>
            <h1 className="mt-5 max-w-3xl text-4xl font-semibold tracking-tight text-slate-950 sm:text-5xl">
              Foundation for the future Air360 landing page and user account
              area.
            </h1>
            <p className="mt-6 max-w-2xl text-base leading-8 text-slate-700 sm:text-lg">
              This application starts as a clean Next.js portal shell. It is
              ready to grow into a public website, authentication flows, and a
              private dashboard for device management.
            </p>
            <div className="mt-8 flex flex-wrap gap-3">
              <a
                className="inline-flex items-center justify-center rounded-full bg-slate-950 px-5 py-3 text-sm font-semibold text-white transition hover:bg-slate-800"
                href="#areas"
              >
                Planned areas
              </a>
              <a
                className="inline-flex items-center justify-center rounded-full border border-slate-300 bg-white px-5 py-3 text-sm font-semibold text-slate-900 transition hover:border-slate-950"
                href="#status"
              >
                Current status
              </a>
            </div>
          </div>

          <aside className="rounded-[2rem] border border-slate-900/10 bg-slate-950 p-8 text-slate-50 shadow-[0_24px_80px_rgba(15,23,42,0.22)]">
            <p className="text-sm font-semibold uppercase tracking-[0.24em] text-emerald-300/80">
              Stack
            </p>
            <ul className="mt-6 space-y-3">
              {projectStack.map((item) => (
                <li
                  key={item}
                  className="rounded-2xl border border-white/10 bg-white/5 px-4 py-3 text-sm text-slate-200"
                >
                  {item}
                </li>
              ))}
            </ul>
            <p className="mt-6 text-sm leading-7 text-slate-300">
              Generated with the official <code>create-next-app</code> tool and
              trimmed to a focused starter for the Air360 portal codebase.
            </p>
          </aside>
        </section>

        <section
          id="areas"
          className="mt-12 grid gap-6 md:grid-cols-2"
          aria-label="Planned portal areas"
        >
          {portalAreas.map((area) => (
            <article
              key={area.title}
              className="rounded-[1.75rem] border border-slate-900/8 bg-white/78 p-7 shadow-[0_20px_60px_rgba(15,23,42,0.06)] backdrop-blur"
            >
              <p className="text-sm font-semibold uppercase tracking-[0.18em] text-emerald-700">
                Scope
              </p>
              <h2 className="mt-4 text-2xl font-semibold tracking-tight text-slate-950">
                {area.title}
              </h2>
              <p className="mt-4 text-sm leading-7 text-slate-700 sm:text-base">
                {area.description}
              </p>
            </article>
          ))}
        </section>

        <section
          id="status"
          className="mt-12 rounded-[2rem] border border-slate-900/8 bg-white/70 p-8 shadow-[0_20px_60px_rgba(15,23,42,0.06)] backdrop-blur"
        >
          <p className="text-sm font-semibold uppercase tracking-[0.24em] text-emerald-800/80">
            Current status
          </p>
          <div className="mt-5 grid gap-4 md:grid-cols-3">
            <div className="rounded-2xl bg-slate-950 px-5 py-5 text-white">
              <p className="text-sm font-medium text-slate-300">
                Implemented now
              </p>
              <p className="mt-3 text-lg font-semibold">
                Initial project scaffold
              </p>
            </div>
            <div className="rounded-2xl border border-slate-200 bg-slate-50 px-5 py-5">
              <p className="text-sm font-medium text-slate-500">Next up</p>
              <p className="mt-3 text-lg font-semibold text-slate-950">
                Auth and account flows
              </p>
            </div>
            <div className="rounded-2xl border border-slate-200 bg-slate-50 px-5 py-5">
              <p className="text-sm font-medium text-slate-500">Documentation</p>
              <p className="mt-3 text-lg font-semibold text-slate-950">
                Ubuntu run and deploy guide in <code>docs/</code>
              </p>
            </div>
          </div>
        </section>
      </main>
    </div>
  );
}
