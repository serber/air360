import type { Metadata } from "next";
import { DeviceDetail } from "@/components/DeviceDetail";
import { PortalNav } from "@/components/PortalShell";

type DevicePageProps = {
  params: Promise<{ public_id: string }>;
};

export async function generateMetadata({
  params,
}: DevicePageProps): Promise<Metadata> {
  await params;

  return {
    title: "Device details",
  };
}

export default async function DevicePage({ params }: DevicePageProps) {
  const { public_id } = await params;

  return (
    <div className="air-page">
      <PortalNav active="map" />
      <DeviceDetail publicId={public_id} />
    </div>
  );
}
