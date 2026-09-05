import { FastifyError, FastifyInstance } from "fastify";

/**
 * Stable `error.code` values for errors raised by Fastify itself (unknown
 * route, malformed JSON, oversized body, schema failure). Route handlers send
 * their own codes (`device_not_found`, `invalid_payload`, ...) directly.
 */
function errorCode(statusCode: number, error: Partial<FastifyError>): string {
  if (statusCode >= 500) return "internal_error";
  if (error.validation) return "validation_error";

  switch (statusCode) {
    case 400:
      return "bad_request";
    case 401:
      return "unauthorized";
    case 404:
      return "not_found";
    case 413:
      return "payload_too_large";
    case 415:
      return "unsupported_media_type";
    case 429:
      return "too_many_requests";
    default:
      return "request_error";
  }
}

export function registerErrorHandler(app: FastifyInstance): void {
  app.setErrorHandler((error, _request, reply) => {
    const err = error as Partial<FastifyError>;
    const statusCode = err.statusCode && err.statusCode >= 400 ? err.statusCode : 500;

    if (statusCode >= 500) {
      app.log.error(error);
    } else {
      app.log.info({ err: error }, "request rejected");
    }

    if (reply.sent) {
      return;
    }

    reply.status(statusCode).send({
      error: {
        code: errorCode(statusCode, err),
        message: statusCode >= 500 ? "Internal server error" : err.message,
      },
    });
  });

  app.setNotFoundHandler((request, reply) => {
    reply.status(404).send({
      error: {
        code: "not_found",
        message: `Route ${request.method}:${request.url} not found`,
      },
    });
  });
}
