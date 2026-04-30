import type { Metadata } from "next";
import { DeviceDetail } from "@/components/DeviceDetail";

type DevicePageProps = {
  params: Promise<{ public_id: string }>;
};

export async function generateMetadata({
  params,
}: DevicePageProps): Promise<Metadata> {
  const { public_id } = await params;

  return {
    title: `Device ${public_id}`,
  };
}

export default async function DevicePage({ params }: DevicePageProps) {
  const { public_id } = await params;

  return <DeviceDetail publicId={public_id} />;
}
