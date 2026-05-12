import Link from "next/link";
import { useTranslations } from "next-intl";
import { HomeMapPreview } from "@/components/HomeMapPreview";
import { ArrowIcon, PortalFooter, PortalNav } from "@/components/PortalShell";
import { PortalStats } from "@/components/PortalStats";

export default function Home() {
  const t = useTranslations("home");

  return (
    <div className="air-page">
      <PortalNav active="home" />
      <main>
        <header className="air-hero">
          <div className="air-container">
            <div className="air-hero-grid">
              <div>
                <h1 className="air-display-1 air-hero-title">
                  {t("titleLine1")}
                  <br />
                  <em>{t("titleLine2")}</em>
                </h1>
                <p className="air-lead">
                  {t("lead")}
                </p>
                <div className="air-hero-cta">
                  <Link href="/map" className="air-btn air-btn-primary air-btn-lg">
                    {t("exploreMap")}
                    <ArrowIcon />
                  </Link>
                </div>
                <PortalStats />
              </div>
              <HomeMapPreview />
            </div>
          </div>
        </header>

      </main>
      <PortalFooter />
    </div>
  );
}
