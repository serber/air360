import { FastifyInstance } from "fastify";

export function registerErrorHandler(app: FastifyInstance): void {
  app.setErrorHandler((error, _request, reply) => {
    app.log.error(error);

    if (reply.sent) {
      return;
    }

    reply.status(500).send({
      error: {
        code: "internal_error",
        message: "Internal server error",
      },
    });
  });
}
