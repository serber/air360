import { FastifyPluginAsync } from "fastify";

interface IngestParams {
  chip_id: string;
  client_batch_id: string;
}

export const ingestRoutes: FastifyPluginAsync = async (app) => {
  app.put<{ Params: IngestParams }>(
    "/devices/:chip_id/batches/:client_batch_id",
    async (request, reply) => {
      reply.status(501).send({
        accepted: false,
        error: {
          code: "not_implemented",
          message: "Measurement ingest is not implemented yet.",
        },
        request: {
          chip_id: request.params.chip_id,
          client_batch_id: request.params.client_batch_id,
        },
      });
    },
  );
};
