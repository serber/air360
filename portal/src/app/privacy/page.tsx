import Link from "next/link";
import { useTranslations } from "next-intl";
import { PortalFooter, PortalNav } from "@/components/PortalShell";
import { CONTACT_EMAIL } from "@/lib/config";

export const metadata = { title: "Privacy Policy" };

export default function Privacy() {
  const t = useTranslations("privacy");
  const deviceData = t.raw("deviceDataItems") as string[];
  const visitorData = t.raw("visitorDataItems") as string[];

  return (
    <div className="air-page">
      <PortalNav active="privacy" />
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
                <span className="air-eyebrow">{t("eyebrow")}</span>
                <h1 className="air-display-2">{t("title")}</h1>
              </div>
              <p className="air-muted">
                {t("lead")}
              </p>
            </div>
          </header>

          <div className="air-doc-grid">
            <article className="air-doc-article">
              <section>
                <h2>{t("introductionTitle")}</h2>
                <p>
                  {t("introductionBody")}
                </p>
                <p>{t("effectiveDate")}</p>
              </section>

              <section>
                <h2>{t("deviceDataTitle")}</h2>
                <p>
                  {t("deviceDataBody")}
                </p>
                <ul>
                  {deviceData.map((item) => (
                    <li key={item}>{item}</li>
                  ))}
                </ul>
                <p>
                  {t("deviceDataPublic")}
                </p>
              </section>

              <section>
                <h2>{t("visitorDataTitle")}</h2>
                <p>
                  {t("visitorDataBody")}
                </p>
                <ul>
                  {visitorData.map((item) => (
                    <li key={item}>{item}</li>
                  ))}
                </ul>
                <p>
                  {t("visitorDataRetention")}
                </p>
              </section>

              <section>
                <h2>{t("cookiesTitle")}</h2>
                <p>
                  {t("cookiesBody")}
                </p>
                <p>
                  {t("mapTilesBody")}
                </p>
              </section>

              <section>
                <h2>{t("sharingTitle")}</h2>
                <p>
                  {t("sharingBody")}
                </p>
              </section>

              <section>
                <h2>{t("securityTitle")}</h2>
                <p>
                  {t("securityBody")}
                </p>
              </section>

              <section>
                <h2>{t("contactTitle")}</h2>
                <p>
                  {t("contactBody")}
                </p>
                <p>
                  <a href={`mailto:${CONTACT_EMAIL}`}>{CONTACT_EMAIL}</a>
                </p>
              </section>
            </article>

            <aside className="air-doc-side">
              <div className="air-info-card">
                <div className="air-info-card-head">
                  <h4>{t("scope")}</h4>
                </div>
                <div className="air-info-card-body">
                  <div className="air-info-row">
                    <span>{t("deviceData")}</span>
                    <b>{t("public")}</b>
                  </div>
                  <div className="air-info-row">
                    <span>{t("visitorLogs")}</span>
                    <b>90 days</b>
                  </div>
                  <div className="air-info-row">
                    <span>{t("trackingAds")}</span>
                    <b>{t("no")}</b>
                  </div>
                  <div className="air-info-row">
                    <span>{t("contact")}</span>
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
