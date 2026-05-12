import Link from "next/link";
import { HomeMapPreview } from "@/components/HomeMapPreview";
import { ArrowIcon, PortalFooter, PortalNav } from "@/components/PortalShell";
import { PortalStats } from "@/components/PortalStats";

export default function Home() {
  return (
    <div className="air-page">
      <PortalNav active="home" />
      <main>
        <header className="air-hero">
          <div className="air-container">
            <div className="air-hero-grid">
              <div>
                <h1 className="air-display-1 air-hero-title">
                  Air quality data
                  <br />
                  <em>from real devices.</em>
                </h1>
                <p className="air-lead">
                  Air360 collects measurements from open-source sensors installed
                  by people in different places. Build a device, connect it, and
                  its data appears on the public map.
                </p>
                <div className="air-hero-cta">
                  <Link href="/map" className="air-btn air-btn-primary air-btn-lg">
                    Explore the map
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
