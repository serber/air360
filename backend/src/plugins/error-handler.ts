import { FastifyInstance } from "fastify";

export function registerErrorHandler(app: FastifyInstance): void {
  app.setErrorHandler((error, _request, reply) => {
    app.log.error(error);

    if (reply.sent) {
      return;
    }

    const err = error as { statusCode?: number; message?: string };
    const statusCode = err.statusCode && err.statusCode >= 400 ? err.statusCode : 500;

    reply.status(statusCode).send({
      error: {
        code: statusCode >= 500 ? "internal_error" : "validation_error",
        message: statusCode >= 500 ? "Internal server error" : err.message,
      },
    });
  });
}
