import Link from "next/link";
import { CONTACT_EMAIL } from "@/lib/config";

export const metadata = { title: "Privacy Policy" };

export default function Privacy() {
  return (
    <main className="min-h-screen bg-[#f4f7f5] px-4 py-8 sm:px-6 lg:px-8">
      <div className="mx-auto max-w-3xl">
        <Link
          href="/"
          className="mb-8 inline-block text-slate-600 hover:text-slate-950"
        >
          ← Back to home
        </Link>

        <div className="rounded-md border border-slate-200 bg-white p-8 shadow-sm">
          <h1 className="text-3xl font-semibold tracking-tight text-slate-950">
            Privacy Policy
          </h1>

          <div className="mt-8 space-y-8 text-slate-600">
            <section>
              <h2 className="text-xl font-semibold text-slate-950">
                Introduction
              </h2>
              <p className="mt-4">
                Air360 is a public portal for environmental monitoring. This
                Privacy Policy describes how we handle data collected through the
                portal.
              </p>
              <p className="mt-3">Effective date: May 3, 2026.</p>
            </section>

            <section>
              <h2 className="text-xl font-semibold text-slate-950">
                Device data
              </h2>
              <p className="mt-4">
                Air360 is a network of IoT devices that collect environmental
                measurements in real time. Each device may report:
              </p>
              <ul className="mt-3 list-inside list-disc space-y-1 pl-2">
                <li>Temperature</li>
                <li>Humidity</li>
                <li>CO₂ concentration</li>
                <li>Particulate matter (PM1.0, PM2.5, PM4.0, PM10)</li>
                <li>Atmospheric pressure</li>
                <li>Illuminance</li>
                <li>NO₂ concentration</li>
                <li>GPS coordinates of the device</li>
              </ul>
              <p className="mt-4">
                All of this data is public and displayed on the portal map. No
                personal information is associated with these measurements.
              </p>
            </section>

            <section>
              <h2 className="text-xl font-semibold text-slate-950">
                Visitor data
              </h2>
              <p className="mt-4">
                When you visit the portal, our server automatically records
                standard access log information:
              </p>
              <ul className="mt-3 list-inside list-disc space-y-1 pl-2">
                <li>IP address</li>
                <li>Date and time of the request</li>
                <li>Browser type and operating system</li>
                <li>Referring URL</li>
              </ul>
              <p className="mt-4">
                These logs are not linked to your identity, are retained for up
                to 90 days, and are used only for technical diagnostics and
                security.
              </p>
            </section>

            <section>
              <h2 className="text-xl font-semibold text-slate-950">Cookies</h2>
              <p className="mt-4">
                Air360 does not use tracking or advertising cookies. The portal
                may set technically necessary cookies required for basic
                functionality.
              </p>
              <p className="mt-4">
                The map is rendered with MapLibre GL, which may request map tiles
                from a third-party tile server. Those requests may include your
                IP address and the map coordinates you are viewing.
              </p>
            </section>

            <section>
              <h2 className="text-xl font-semibold text-slate-950">
                Third-party sharing
              </h2>
              <p className="mt-4">
                Air360 does not sell, rent, or share visitor data with third
                parties for commercial purposes. Data may be disclosed only when
                required by applicable law, such as in response to a lawful
                request from law enforcement.
              </p>
            </section>

            <section>
              <h2 className="text-xl font-semibold text-slate-950">
                Security
              </h2>
              <p className="mt-4">
                Air360 applies standard technical measures to protect its systems
                and data. The portal uses HTTPS to encrypt data in transit
                between your browser and our server.
              </p>
            </section>

            <section>
              <h2 className="text-xl font-semibold text-slate-950">Contact</h2>
              <p className="mt-4">
                If you have questions about this Privacy Policy or how we handle
                data, please contact us at:
              </p>
              <p className="mt-3">
                <a
                  href={`mailto:${CONTACT_EMAIL}`}
                  className="text-emerald-700 hover:underline"
                >
                  {CONTACT_EMAIL}
                </a>
              </p>
            </section>
          </div>
        </div>
      </div>
    </main>
  );
}
