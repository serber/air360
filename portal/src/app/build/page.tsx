import Link from "next/link";
import { BuildImagePreview } from "@/components/BuildImagePreview";
import { useTranslations } from "next-intl";
import { ArrowIcon, PortalFooter, PortalNav } from "@/components/PortalShell";

export const metadata = { title: "Build Guide" };

type BuildOption = {
  id: string;
  title: string;
  label: string;
  body: string;
  notes: string[];
  links?: Array<{
    href: string;
    label: string;
  }>;
  ctaState: string;
};

type WiringRow = {
  sensors: string[];
  transport: string;
  pins: string;
  note: string;
};

type ShieldPart = {
  component: string;
  quantity: string;
  use: string;
  search: string;
};

type FirmwareStep = {
  title: string;
  body: string;
};

type FirmwareAccess = {
  label: string;
  value: string;
  note: string;
};

type CompatibilityRow = {
  sensor: string;
  sensorCommunity: string;
  air360Api: string;
};

type DifferenceItem = {
  title: string;
  body: string;
};

export default function BuildGuide() {
  const t = useTranslations("build");
  const options = t.raw("options") as BuildOption[];
  const shieldParts = t.raw("shieldParts") as ShieldPart[];
  const wiringRows = t.raw("wiringRows") as WiringRow[];
  const directNotes = t.raw("directNotes") as string[];
  const firmwareAccess = t.raw("firmwareAccess") as FirmwareAccess[];
  const firmwareSteps = t.raw("firmwareSteps") as FirmwareStep[];
  const compatibilityRows = t.raw("compatibilityRows") as CompatibilityRow[];
  const differenceItems = t.raw("differenceItems") as DifferenceItem[];

  return (
    <div className="air-page">
      <PortalNav active="build" />
      <main className="air-doc-page">
        <div className="air-container">
          <header className="air-doc-head">
            <div className="air-crumb">
              <Link href="/">{t("backToHome")}</Link>
              <span>/</span>
              <span>{t("crumb")}</span>
            </div>
            <div className="air-doc-title-grid">
              <div>
                <h1 className="air-display-2">{t("title")}</h1>
              </div>
            </div>
          </header>

          <div className="air-doc-grid air-doc-grid-full">
            <article className="air-doc-article air-build-guide">
              <section>
                <h2>{t("assemblyTitle")}</h2>
                <p>{t("assemblyBody")}</p>

                <div className="air-build-option-grid">
                  {options.map((option) => (
                    <div className="air-build-option" key={option.id}>
                      <div className="air-build-option-head">
                        <span className="air-tag">{option.label}</span>
                        <span>{option.ctaState}</span>
                      </div>
                      <h3>{option.title}</h3>
                      <p>{option.body}</p>
                      <ul>
                        {option.notes.map((note) => (
                          <li key={note}>{note}</li>
                        ))}
                      </ul>
                      {option.links?.length ? (
                        <div className="air-build-link-list">
                          {option.links.map((link) => (
                            <a
                              className="air-build-link-placeholder"
                              href={link.href}
                              key={link.href}
                              rel="noreferrer"
                              target="_blank"
                            >
                              {link.label}
                              <ArrowIcon />
                            </a>
                          ))}
                        </div>
                      ) : null}
                    </div>
                  ))}
                </div>
              </section>

              <section>
                <h2>{t("shieldTitle")}</h2>
                <p>{t("shieldBody")}</p>
                <BuildImagePreview
                  images={[
                    {
                      alt: t("shieldImageTopAlt"),
                      src: "https://github.com/serber/air360/blob/main/docs/hardware/air360_shield_v1.3_top.jpg?raw=true",
                    },
                    {
                      alt: t("shieldImageSolderedAlt"),
                      src: "https://github.com/serber/air360/blob/main/docs/hardware/air360_shield_v1.3_soldered_overview.jpg?raw=true",
                    },
                  ]}
                />

                <div className="air-wiring-table-wrap">
                  <table className="air-wiring-table">
                    <thead>
                      <tr>
                        <th>{t("componentColumn")}</th>
                        <th>{t("quantityColumn")}</th>
                        <th>{t("useColumn")}</th>
                        <th>{t("searchColumn")}</th>
                      </tr>
                    </thead>
                    <tbody>
                      {shieldParts.map((part) => (
                        <tr key={part.component}>
                          <td>{part.component}</td>
                          <td>{part.quantity}</td>
                          <td>{part.use}</td>
                          <td>{part.search}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </section>

              <section>
                <h2>{t("directTitle")}</h2>
                <p>{t("directBody")}</p>
                <ul>
                  {directNotes.map((note) => (
                    <li key={note}>{note}</li>
                  ))}
                </ul>

                <div className="air-wiring-table-wrap">
                  <table className="air-wiring-table">
                    <thead>
                      <tr>
                        <th>{t("sensorColumn")}</th>
                        <th>{t("transportColumn")}</th>
                        <th>{t("pinsColumn")}</th>
                        <th>{t("noteColumn")}</th>
                      </tr>
                    </thead>
                    <tbody>
                      {wiringRows.map((row) => (
                        <tr key={row.sensors.join("|")}>
                          <td>
                            <ul className="air-wiring-sensor-list">
                              {row.sensors.map((sensor) => (
                                <li key={sensor}>{sensor}</li>
                              ))}
                            </ul>
                          </td>
                          <td>{row.transport}</td>
                          <td>{row.pins}</td>
                          <td>{row.note}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </section>

              <section className="air-firmware-section">
                <h2>{t("firmwareTitle")}</h2>
                <p>{t("firmwareBody")}</p>
                <div className="air-build-link-list air-build-link-list-inline air-firmware-top-links">
                  <a
                    className="air-btn air-btn-brand"
                    href="https://github.com/serber/air360/releases"
                    rel="noreferrer"
                    target="_blank"
                  >
                    {t("firmwareReleaseLink")}
                    <ArrowIcon />
                  </a>
                  <a
                    className="air-btn"
                    href="https://espflash.app/"
                    rel="noreferrer"
                    target="_blank"
                  >
                    {t("espFlashLink")}
                    <ArrowIcon />
                  </a>
                </div>

                <div className="air-firmware-panel">
                  <div className="air-firmware-panel-head">
                    <div>
                      <h3>{t("firmwarePanelTitle")}</h3>
                    </div>
                  </div>

                  <div className="air-firmware-access-grid">
                    {firmwareAccess.map((item) => (
                      <div className="air-firmware-access-card" key={item.label}>
                        <span>{item.label}</span>
                        <b>{item.value}</b>
                        <p>{item.note}</p>
                      </div>
                    ))}
                  </div>
                </div>

                <div className="air-firmware-step-grid">
                  {firmwareSteps.map((step, index) => (
                    <div className="air-firmware-step" key={step.title}>
                      <span>{index + 1}</span>
                      <h3>{step.title}</h3>
                      <p>{step.body}</p>
                    </div>
                  ))}
                </div>

                <BuildImagePreview
                  images={[
                    {
                      alt: t("firmwareOverviewImageAlt"),
                      src: "https://github.com/serber/air360/blob/main/docs/firmware/images/firmware_overview.png?raw=true",
                    },
                    {
                      alt: t("firmwareDeviceImageAlt"),
                      src: "https://github.com/serber/air360/blob/main/docs/firmware/images/firmware_device.png?raw=true",
                    },
                    {
                      alt: t("firmwareSensorsImageAlt"),
                      src: "https://github.com/serber/air360/blob/main/docs/firmware/images/firmware_sensors.png?raw=true",
                    },
                    {
                      alt: t("firmwareBackendsImageAlt"),
                      src: "https://github.com/serber/air360/blob/main/docs/firmware/images/firmware_backends.png?raw=true",
                    },
                  ]}
                />
              </section>

              <section>
                <h2>{t("firmwareDocsTitle")}</h2>
                <p>{t("firmwareDocsBody")}</p>
                <div className="air-build-link-list air-build-link-list-inline">
                  <a
                    className="air-btn air-btn-brand"
                    href="https://github.com/serber/air360/blob/main/docs/firmware/user-guide.md"
                    rel="noreferrer"
                    target="_blank"
                  >
                    {t("firmwareDocsLink")}
                    <ArrowIcon />
                  </a>
                </div>
              </section>

              <section>
                <h2>{t("enclosureTitle")}</h2>
                <p>{t("enclosureBody")}</p>
                <div className="air-build-link-list air-build-link-list-inline">
                  <a
                    className="air-btn air-btn-brand"
                    href="https://www.printables.com/model/1743061-air360-stevenson-screen-enclosure"
                    rel="noreferrer"
                    target="_blank"
                  >
                    {t("enclosureLink")}
                    <ArrowIcon />
                  </a>
                </div>
                <BuildImagePreview
                  className="air-build-photo-grid-centered"
                  images={[
                    {
                      alt: t("enclosureImageAlt"),
                      src: "https://github.com/serber/air360/blob/main/docs/hardware/air360_stevenson_screen.jpg?raw=true",
                    },
                  ]}
                />
              </section>

              <section>
                <h2>{t("compatibilityTitle")}</h2>
                <p>{t("compatibilityBody")}</p>
                <div className="air-wiring-table-wrap">
                  <table className="air-wiring-table">
                    <thead>
                      <tr>
                        <th>{t("compatibilitySensorColumn")}</th>
                        <th>{t("compatibilitySensorCommunityColumn")}</th>
                        <th>{t("compatibilityAir360Column")}</th>
                      </tr>
                    </thead>
                    <tbody>
                      {compatibilityRows.map((row) => (
                        <tr key={row.sensor}>
                          <td>{row.sensor}</td>
                          <td>{row.sensorCommunity}</td>
                          <td>{row.air360Api}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </section>

              <section>
                <h2>{t("differencesTitle")}</h2>
                <p>{t("differencesBody")}</p>
                <div className="air-difference-grid">
                  {differenceItems.map((item) => (
                    <div className="air-difference-card" key={item.title}>
                      <h3>{item.title}</h3>
                      <p>{item.body}</p>
                    </div>
                  ))}
                </div>
              </section>
            </article>

          </div>
        </div>
      </main>
      <PortalFooter />
    </div>
  );
}
