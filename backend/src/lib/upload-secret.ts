import { createHash, timingSafeEqual } from "node:crypto";

export function hashUploadSecret(secret: string): string {
  const digest = createHash("sha256").update(secret).digest("base64url");
  return `sha256:${digest}`;
}

export function verifyUploadSecret(secret: string, storedHash: string): boolean {
  const computed = hashUploadSecret(secret);
  if (computed.length !== storedHash.length) return false;
  return timingSafeEqual(Buffer.from(computed), Buffer.from(storedHash));
}
