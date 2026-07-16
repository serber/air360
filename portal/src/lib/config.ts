// Portal pages are dynamically rendered, so this is read from the server
// environment at request time (e.g. from /etc/air360-portal.env via systemd).
export const CONTACT_EMAIL = process.env.CONTACT_EMAIL?.trim() ?? "";
