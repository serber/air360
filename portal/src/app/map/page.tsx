import { DeviceMapLoader } from "@/components/DeviceMapLoader";
import { PortalNav } from "@/components/PortalShell";

export const metadata = {
  title: "Device Map",
};

export default function MapPage() {
  return (
    <div className="air-page">
      <PortalNav active="map" />
      <DeviceMapLoader />
    </div>
  );
}
