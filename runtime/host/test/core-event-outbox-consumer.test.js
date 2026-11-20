import test from "node:test";
import assert from "node:assert/strict";
import { createCoreEventOutboxConsumer, CoreEventOutboxError } from "../src/core-event-outbox-consumer.js";

const serverId = "22222222-2222-4222-8222-222222222222";
const credential = `cg_srv_${"s".repeat(40)}`;
const leaseToken = "33333333-3333-4333-8333-333333333333";
const outboxId = "44444444-4444-4444-8444-444444444444";

function makeRow(overrides = {}) {
  return {
    id: outboxId,
    eventType: "game.event.received",
    aggregateType: "game_event",
    aggregateId: "55555555-5555-4555-8555-555555555555",
    payload: {
      eventRecordId: "55555555-5555-4555-8555-555555555555",
      serverId,
      clusterId: "11111111-1111-4111-8111-111111111111",
      gridId: "A1",
      eventId: "external-event-42",
      eventType: "tribe.member.joined",
      occurredAt: "2026-11-16T00:00:00.000Z",
      payload: { player: { name: "TestSailor" } }
    },
    occurredAt: "2026-11-16T00:00:00.000Z",
    attempts: 1,
    leaseToken,
    leaseExpiresAt: "2026-11-16T00:02:00.000Z",
    ...overrides
  };
}

function response(status, result) {
  return new Response(JSON.stringify(result), { status, headers: { "content-type": "application/json" } });
}

function makeConsumer(overrides = {}) {
  return createCoreEventOutboxConsumer({
    baseUrl: "https://core.example.test/api/v1",
    credential,
    serverId,
    processEvent: async () => {},
    fetchImpl: async () => response(200, { data: [] }),
    ...overrides
  });
}

test("requires valid host configuration and a handler", () => {
  assert.throws(() => createCoreEventOutboxConsumer({}), /baseUrl/);
  assert.throws(() => makeConsumer({ credential: "bad" }), /service credential/);
  assert.throws(() => makeConsumer({ serverId: "bad" }), /serverId/);
  assert.throws(() => makeConsumer({ processEvent: null }), /processEvent/);
  assert.throws(() => makeConsumer({ limit: 0 }), /limit/);
  assert.throws(() => makeConsumer({ limit: 2 }), /limit must be 1/);
  assert.throws(() => makeConsumer({ leaseSeconds: 10 }), /leaseSeconds/);
  assert.throws(() => makeConsumer({ eventTypes: [] }), /eventTypes/);
});

test("claims scoped events and acknowledges with the outbox UUID and lease token", async () => {
  const calls = [];
  const handled = [];
  const consumer = makeConsumer({
    eventTypes: ["tribe.member.joined"],
    processEvent: async (event) => handled.push(event),
    fetchImpl: async (url, options) => {
      calls.push({ url: String(url), ...options, body: JSON.parse(options.body) });
      if (calls.length === 1) return response(200, { data: [makeRow()] });
      return response(202, { data: { accepted: true, duplicate: false, eventId: outboxId } });
    }
  });
  const result = await consumer.pollOnce();
  assert.deepEqual(result, { claimed: 1, delivered: 1, failed: 0, acknowledgementErrors: [] });
  assert.equal(handled.length, 1);
  assert.equal(handled[0].id, outboxId);
  assert.equal(handled[0].payload.eventId, "external-event-42");
  assert.equal(handled[0].payload.eventType, "tribe.member.joined");
  assert.equal(calls[0].url, `https://core.example.test/api/v1/integrations/servers/${serverId}/events/claim`);
  assert.deepEqual(calls[0].body, { limit: 1, leaseSeconds: 120, eventTypes: ["tribe.member.joined"] });
  assert.equal(calls[0].headers.authorization, `Bearer ${credential}`);
  assert.equal(calls[1].url, `https://core.example.test/api/v1/integrations/servers/${serverId}/events/${outboxId}/ack`);
  assert.deepEqual(calls[1].body, { leaseToken, outcome: "delivered" });
});

test("rejects multi-event responses before invoking any handler", async () => {
  let handled = 0;
  const secondId = "66666666-6666-4666-8666-666666666666";
  const consumer = makeConsumer({
    processEvent: async () => { handled += 1; },
    fetchImpl: async () => response(200, { data: [makeRow(), makeRow({ id: secondId })] })
  });
  await assert.rejects(consumer.pollOnce(), /more than one event for a single-event claim/);
  assert.equal(handled, 0);
});

test("records handler failure through Core's retryable failed acknowledgement", async () => {
  const calls = [];
  const consumer = makeConsumer({
    processEvent: async () => { throw new Error(`failure included ${credential}`); },
    fetchImpl: async (url, options) => {
      calls.push({ url: String(url), body: JSON.parse(options.body) });
      return calls.length === 1
        ? response(200, { data: [makeRow()] })
        : response(202, { data: { accepted: true, duplicate: false, eventId: outboxId } });
    }
  });
  const result = await consumer.pollOnce();
  assert.equal(result.failed, 1);
  assert.equal(result.delivered, 0);
  assert.equal(calls[1].body.outcome, "failed");
  assert.equal(calls[1].body.leaseToken, leaseToken);
  assert.equal(calls[1].body.error.includes(credential), false);
  assert.match(calls[1].body.error, /\[redacted\]/);
  assert.ok(calls[1].body.error.length <= 1000);
});

test("reports acknowledgement failure so lease expiry can safely permit redelivery", async () => {
  const consumer = makeConsumer({
    fetchImpl: async (url) => String(url).endsWith("/claim")
      ? response(200, { data: [makeRow()] })
      : response(409, { error: { code: "EVENT_LEASE_INVALID" } })
  });
  const result = await consumer.pollOnce();
  assert.equal(result.claimed, 1);
  assert.equal(result.delivered, 0);
  assert.equal(result.acknowledgementErrors.length, 1);
  assert.equal(result.acknowledgementErrors[0].eventId, outboxId);
});

test("rejects malformed claim responses and out-of-scope rows before invoking the handler", async () => {
  let handled = 0;
  const badClaim = makeConsumer({ fetchImpl: async () => response(200, { data: {} }), processEvent: async () => { handled += 1; } });
  await assert.rejects(badClaim.pollOnce(), /invalid outbox claim response/);
  const badRow = makeConsumer({ fetchImpl: async () => response(200, { data: [makeRow({ payload: { ...makeRow().payload, serverId: "77777777-7777-4777-8777-777777777777" } })] }), processEvent: async () => { handled += 1; } });
  const result = await badRow.pollOnce();
  assert.equal(handled, 0);
  assert.equal(result.acknowledgementErrors.length, 1);
});

test("prevents overlapping polls on one consumer instance", async () => {
  let release;
  const consumer = makeConsumer({ fetchImpl: async () => new Promise((resolve) => { release = () => resolve(response(200, { data: [] })); }) });
  const first = consumer.pollOnce();
  await new Promise((resolve) => setImmediate(resolve));
  await assert.rejects(consumer.pollOnce(), (error) => error instanceof CoreEventOutboxError && /already in progress/.test(error.message));
  release();
  await first;
});
