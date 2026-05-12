import Link from "next/link";
import { PortalFooter, PortalNav } from "@/components/PortalShell";
import { CONTACT_EMAIL } from "@/lib/config";

export const metadata = { title: "Privacy Policy" };

const deviceData = [
  "Temperature",
  "Humidity",
  "CO2 concentration",
  "Particulate matter (PM1.0, PM2.5, PM4.0, PM10)",
  "Atmospheric pressure",
  "Illuminance",
  "NO2 concentration",
  "GPS coordinates of the device",
];

const visitorData = [
  "IP address",
  "Date and time of the request",
  "Browser type and operating system",
  "Referring URL",
];

export default function Privacy() {
  return (
    <div className="air-page">
      <PortalNav active="privacy" />
      <main className="air-doc-page">
        <div className="air-container">
          <header className="air-doc-head">
            <div className="air-crumb">
              <Link href="/">← Back to home</Link>
              <span>/</span>
              <span>PRIVACY</span>
            </div>
            <div className="air-doc-title-grid">
              <div>
                <span className="air-eyebrow">Policy</span>
                <h1 className="air-display-2">Privacy Policy</h1>
              </div>
              <p className="air-muted">
                Air360 is a public portal for environmental monitoring. This
                policy describes how we handle device data, visitor logs, and
                third-party map tile requests.
              </p>
            </div>
          </header>

          <div className="air-doc-grid">
            <article className="air-doc-article">
              <section>
                <h2>Introduction</h2>
                <p>
                  Air360 is a public portal for environmental monitoring. This
                  Privacy Policy describes how we handle data collected through
                  the portal.
                </p>
                <p>Effective date: May 3, 2026.</p>
              </section>

              <section>
                <h2>Device data</h2>
                <p>
                  Air360 is a network of IoT devices that collect environmental
                  measurements in real time. Each device may report:
                </p>
                <ul>
                  {deviceData.map((item) => (
                    <li key={item}>{item}</li>
                  ))}
                </ul>
                <p>
                  All of this data is public and displayed on the portal map. No
                  personal information is associated with these measurements.
                </p>
              </section>

              <section>
                <h2>Visitor data</h2>
                <p>
                  When you visit the portal, our server automatically records
                  standard access log information:
                </p>
                <ul>
                  {visitorData.map((item) => (
                    <li key={item}>{item}</li>
                  ))}
                </ul>
                <p>
                  These logs are not linked to your identity, are retained for up
                  to 90 days, and are used only for technical diagnostics and
                  security.
                </p>
              </section>

              <section>
                <h2>Cookies</h2>
                <p>
                  Air360 does not use tracking or advertising cookies. The portal
                  may set technically necessary cookies required for basic
                  functionality.
                </p>
                <p>
                  The map is rendered with MapLibre GL, which may request map
                  tiles from a third-party tile server. Those requests may
                  include your IP address and the map coordinates you are
                  viewing.
                </p>
              </section>

              <section>
                <h2>Third-party sharing</h2>
                <p>
                  Air360 does not sell, rent, or share visitor data with third
                  parties for commercial purposes. Data may be disclosed only
                  when required by applicable law, such as in response to a
                  lawful request from law enforcement.
                </p>
              </section>

              <section>
                <h2>Security</h2>
                <p>
                  Air360 applies standard technical measures to protect its
                  systems and data. The portal uses HTTPS to encrypt data in
                  transit between your browser and our server.
                </p>
              </section>

              <section>
                <h2>Contact</h2>
                <p>
                  If you have questions about this Privacy Policy or how we
                  handle data, please contact us at:
                </p>
                <p>
                  <a href={`mailto:${CONTACT_EMAIL}`}>{CONTACT_EMAIL}</a>
                </p>
              </section>
            </article>

            <aside className="air-doc-side">
              <div className="air-info-card">
                <div className="air-info-card-head">
                  <h4>Scope</h4>
                </div>
                <div className="air-info-card-body">
                  <div className="air-info-row">
                    <span>Device data</span>
                    <b>Public</b>
                  </div>
                  <div className="air-info-row">
                    <span>Visitor logs</span>
                    <b>90 days</b>
                  </div>
                  <div className="air-info-row">
                    <span>Tracking ads</span>
                    <b>No</b>
                  </div>
                  <div className="air-info-row">
                    <span>Contact</span>
                    <b>{CONTACT_EMAIL}</b>
                  </div>
                </div>
              </div>
            </aside>
          </div>
        </div>
      </main>
      <PortalFooter />
    </div>
  );
}
