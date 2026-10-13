/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "InputSequenceTests.h"
#include "deskflow/InputCutoffProtocol.h"
#include "deskflow/InputPreparationProtocol.h"
#include "deskflow/InputSequence.h"
#include <thread>

using namespace deskflow::windowshare;
namespace {
InputSequenceToken token()
{
  return {0x01020304, 0x1122334455667788, {0x99aabbccddeeff01, 0x123456789abcdef0, 16}};
}
std::string bytes(const char *hex)
{
  return QByteArray::fromHex(hex).toStdString();
}
} // namespace

void InputSequenceTests::wireLayoutPreservesFullWidthCapture()
{
  const auto payload = bytes("444d4d5680007fff");
  const auto frame = encodeInputSequenceEnvelope(token(), payload);
  QCOMPARE(frame, bytes("44534551002601020304112233445566778899aabbccddeeff01123456789abcdef00010444d4d5680007fff"));
  const auto decoded = decodeInputSequenceEnvelope(std::string_view(frame).substr(6));
  QVERIFY(decoded);
  QVERIFY(decoded->token == token());
  QCOMPARE(decoded->payload, payload);
  auto invalid = token();
  invalid.capture.part = 0;
  QVERIFY_EXCEPTION_THROWN(encodeInputSequenceEnvelope(invalid, payload), std::invalid_argument);
  invalid.capture.part = 17;
  QVERIFY(!validInputSequenceToken(invalid));
}

void InputSequenceTests::framingRejectsTruncationControlAndHistoricalRepeat()
{
  const auto frame = encodeInputSequenceEnvelope(token(), bytes("444d4d5600010002"));
  const auto body = std::string_view(frame).substr(6);
  for (std::size_t size = 0; size < body.size(); ++size)
    QVERIFY(!decodeInputSequenceEnvelope(body.substr(0, size)));
  QVERIFY(!decodeInputSequenceEnvelope(std::string(body) + '\0'));
  QVERIFY(!validInputSequencePayload("CNOP"));
  QVERIFY(!validInputSequencePayload("CROP"));
  QVERIFY(!validInputSequencePayload(frame));
  QVERIFY(!validInputSequencePayload(bytes("444b52500001000200030004")));
  QVERIFY(validInputSequencePayload(bytes("444b5250000100020003000400000000")));
  QVERIFY(validInputSequencePayload(bytes("444b444c00010002000300000002656e")));
  QVERIFY(!validInputSequencePayload(bytes("444b444c00010002000300000003656e")));
  QVERIFY(!validInputSequencePayload(bytes("444b444c000100020003ffffffff")));
  auto maximum = bytes("444b444c000100020003000003f2") + std::string(1010, 'x');
  QCOMPARE(maximum.size(), inputSequenceMaximumPayload);
  QVERIFY(validInputSequencePayload(maximum));
  const auto bounded = encodeInputSequenceEnvelope(token(), maximum);
  QVERIFY(decodeInputSequenceEnvelope(std::string_view(bounded).substr(6)));
  maximum.push_back('x');
  QVERIFY(!validInputSequencePayload(maximum));
}

void InputSequenceTests::receiptRequiresConsistentTerminalOutcome()
{
  const InputSequenceReceipt receipt{0x01020304, 0x1122334455667788, InputSequenceOutcome::Expanded, 2};
  const auto frame = encodeInputSequenceReceipt(receipt);
  QCOMPARE(frame, bytes("43495352010203041122334455667788040002"));
  QVERIFY(decodeInputSequenceReceipt(std::string_view(frame).substr(4)) == receipt);
  for (std::size_t size = 0; size < inputSequenceReceiptBodyBytes; ++size)
    QVERIFY(!decodeInputSequenceReceipt(std::string_view(frame).substr(4, size)));
  for (auto outcome : {InputSequenceOutcome::Unsupported, InputSequenceOutcome::Filtered}) {
    QVERIFY(validInputSequenceReceipt({1, 1, outcome, 0}));
    QVERIFY(!validInputSequenceReceipt({1, 1, outcome, 1}));
  }
  QVERIFY(!validInputSequenceReceipt({1, 1, InputSequenceOutcome::Submitted, 0}));
  QVERIFY(!validInputSequenceReceipt({1, 1, InputSequenceOutcome::Submitted, 2}));
  QVERIFY(!validInputSequenceReceipt({1, 1, InputSequenceOutcome::Expanded, 1}));
  QVERIFY(validInputSequenceReceipt({1, 1, InputSequenceOutcome::Failed, 0}));
  QVERIFY(validInputSequenceReceipt({1, 1, InputSequenceOutcome::Failed, 12}));
  QVERIFY(!validInputSequenceReceipt({1, 1, static_cast<InputSequenceOutcome>(6), 0}));
  QVERIFY(!validInputSequenceReceipt({0, 1, InputSequenceOutcome::Filtered, 0}));
}

void InputSequenceTests::scopeSeparatesSubmissionFromDelivery()
{
  QVERIFY(!InputSequenceSubmissionScope::currentToken());
  InputSequenceSubmissionScope::submitted();
  {
    InputSequenceSubmissionScope scope(token());
    QVERIFY(InputSequenceSubmissionScope::currentToken() == token());
    QCOMPARE(scope.receipt().outcome, InputSequenceOutcome::Unsupported);
    InputSequenceSubmissionScope::backendParticipated();
    QCOMPARE(scope.receipt().outcome, InputSequenceOutcome::Filtered);
    bool isolated = false;
    std::thread other([&] {
      isolated = !InputSequenceSubmissionScope::currentToken();
      InputSequenceSubmissionScope::submitted();
    });
    other.join();
    QVERIFY(isolated);
    QCOMPARE(scope.receipt().submissions, 0);
    InputSequenceSubmissionScope::submitted();
    QCOMPARE(scope.receipt().outcome, InputSequenceOutcome::Submitted);
    InputSequenceSubmissionScope::submitted();
    QCOMPARE(scope.receipt().outcome, InputSequenceOutcome::Expanded);
    InputSequenceSubmissionScope::fail();
    QCOMPARE(scope.receipt().outcome, InputSequenceOutcome::Failed);
  }
  QVERIFY(!InputSequenceSubmissionScope::currentToken());
  {
    InputSequenceSubmissionScope scope(token());
    InputSequenceSubmissionScope::filtered();
    QCOMPARE(scope.receipt().outcome, InputSequenceOutcome::Filtered);
    InputSequenceSubmissionScope::submitted();
    QCOMPARE(scope.receipt().outcome, InputSequenceOutcome::Failed);
  }
}

void InputSequenceTests::nestedAndExhaustedScopesFail()
{
  {
    InputSequenceSubmissionScope outer(token());
    {
      InputSequenceSubmissionScope inner(token());
      QCOMPARE(inner.receipt().outcome, InputSequenceOutcome::Failed);
    }
    QCOMPARE(outer.receipt().outcome, InputSequenceOutcome::Failed);
  }
  InputSequenceSubmissionScope scope(token());
  for (unsigned index = 0; index <= std::numeric_limits<std::uint16_t>::max(); ++index)
    InputSequenceSubmissionScope::submitted();
  QCOMPARE(scope.receipt().outcome, InputSequenceOutcome::Failed);
  QCOMPARE(scope.receipt().submissions, std::numeric_limits<std::uint16_t>::max());
}

void InputSequenceTests::originScopeDoesNotLeak()
{
  QVERIFY(!InputSequenceOriginScope::current());
  {
    InputSequenceOriginScope original(token().capture);
    QVERIFY(InputSequenceOriginScope::current() == token().capture);
    {
      InputSequenceOriginScope noOrigin({});
      QVERIFY(!InputSequenceOriginScope::current());
    }
    QVERIFY(InputSequenceOriginScope::current() == token().capture);
  }
  QVERIFY(!InputSequenceOriginScope::current());
}

void InputSequenceTests::inputCutoffCodecIsBoundedAndExact()
{
  InputCutoffBinding binding{
      0xf102030405060708ULL,
      0x8899aabb,
      {0xfedcba9876543210ULL, 0xabcdef0198765432ULL, 2},
      {{11, 12}, 13, 14, 15, 16, ForwardingScope::All},
      0xaabbccddeeff1122ULL
  };
  const auto request = encodeInputCutoffRequest({binding, 250});
  QCOMPARE(request.size(), 4 + inputCutoffRequestBytes);
  const auto decoded = decodeInputCutoffRequest(std::string_view(request).substr(4));
  QVERIFY(decoded && decoded->binding == binding && decoded->remainingMs == 250);
  for (std::size_t size = 0; size < inputCutoffRequestBytes; ++size)
    QVERIFY(!decodeInputCutoffRequest(std::string_view(request).substr(4, size)));
  QVERIFY(!decodeInputCutoffRequest(std::string_view(request).substr(4).substr(0, inputCutoffRequestBytes - 1)));
  QVERIFY(!decodeInputCutoffRequest(request.substr(4) + '\0'));
  QVERIFY_EXCEPTION_THROWN(encodeInputCutoffRequest({binding, 0}), std::invalid_argument);
  QVERIFY_EXCEPTION_THROWN(encodeInputCutoffRequest({binding, 251}), std::invalid_argument);
  const auto resume = encodeInputCutoffResume(binding);
  QVERIFY(decodeInputCutoffResume(std::string_view(resume).substr(4)) == binding);
  for (auto state : {InputCutoffState::BackendObserved, InputCutoffState::Denied, InputCutoffState::Resumed}) {
    const InputCutoffReceipt receipt{binding, state};
    const auto bytes = encodeInputCutoffReceipt(receipt);
    QCOMPARE(bytes.size(), 4 + inputCutoffReceiptBytes);
    QVERIFY(decodeInputCutoffReceipt(std::string_view(bytes).substr(4)) == receipt);
    for (std::size_t size = 0; size < inputCutoffReceiptBytes; ++size)
      QVERIFY(!decodeInputCutoffReceipt(std::string_view(bytes).substr(4, size)));
  }
  auto wrong = binding;
  wrong.forwarding.scope = ForwardingScope::Keyboard;
  QVERIFY(!validInputCutoffBinding(wrong));
  QVERIFY_EXCEPTION_THROWN(encodeInputCutoffRequest({wrong, 50}), std::invalid_argument);
  wrong = binding;
  wrong.viewerConnection = 0;
  QVERIFY(!validInputCutoffBinding(wrong));
  wrong = binding;
  wrong.capture.part = 0;
  QVERIFY(!validInputCutoffBinding(wrong));
  QVERIFY_EXCEPTION_THROWN(encodeInputCutoffReceipt({binding, InputCutoffState::Pending}), std::invalid_argument);
  QVERIFY_EXCEPTION_THROWN(encodeInputCutoffReceipt({binding, InputCutoffState::Resuming}), std::invalid_argument);
  auto localState = encodeInputCutoffReceipt({binding, InputCutoffState::Denied});
  localState.back() = static_cast<char>(InputCutoffState::Resuming);
  QVERIFY(!decodeInputCutoffReceipt(std::string_view(localState).substr(4)));
}

void InputSequenceTests::mappedCutoffCodecPreservesPointAndProof()
{
  static_assert(!std::is_default_constructible_v<InputCutoffObservation>);
  InputCutoffMappedResult result;
  result.binding = {
      0xfedcba9876543210ULL,
      0xfedcba98,
      {0xabcdef0123456789ULL, 0x1234567890abcdefULL, 3},
      {{31, 32}, 33, 34, 35, 36, ForwardingScope::All},
      37
  };
  result.point = {-9999999, 10000000};
  result.sourceX = 123.25;
  result.sourceY = 456.75;
  result.sourceWidth = 1920;
  result.sourceHeight = 1080;
  result.surfaceIncarnation = 0xfedcba9876543210ULL;
  result.viewportRevision = 0x1234567890abcdefULL;
  result.nativeSurface = 41;
  result.providerGeneration = 42;
  result.nativeRevision = 43;
  result.viewerPeerFingerprint.fill(0xab);
  result.orderingContract = "test-reviewed-build:123";
  const auto request = encodeInputCutoffMappedRequest({{result.binding, 250}, result.point});
  const auto decoded = decodeInputCutoffMappedRequest(std::string_view(request).substr(4));
  QVERIFY(decoded && decoded->cutoff.binding == result.binding && decoded->point == result.point);
  QCOMPARE(request.size(), inputCutoffMappedRequestBytes + 4);
  const auto receipt = encodeInputCutoffMappedResult(result);
  QCOMPARE(receipt.size(), inputCutoffMappedResultBytes + 4);
  QCOMPARE(decodeInputCutoffMappedResult(std::string_view(receipt).substr(4)), std::optional(result));
  const auto sealed = encodeInputCutoffSealedResult({result, result.binding.request - 1});
  QCOMPARE(sealed.size(), inputCutoffSealedResultBytes + 4);
  const auto decodedSeal = decodeInputCutoffSealedResult(std::string_view(sealed).substr(4));
  QVERIFY(decodedSeal && decodedSeal->mapping == result && decodedSeal->sealRequest == result.binding.request - 1);
  for (std::size_t size = 0; size < inputCutoffSealedResultBytes; ++size)
    QVERIFY(!decodeInputCutoffSealedResult(std::string_view(sealed).substr(4, size)));
  QVERIFY(!decodeInputCutoffSealedResult(sealed.substr(4) + '\0'));
  for (std::size_t size = 0; size < inputCutoffMappedResultBytes; ++size)
    QVERIFY(!decodeInputCutoffMappedResult(std::string_view(receipt).substr(4, size)));
  QVERIFY(!decodeInputCutoffMappedResult(receipt.substr(4) + '\0'));
  auto padded = receipt.substr(4);
  padded.back() = 'x';
  QVERIFY(!decodeInputCutoffMappedResult(padded));
  auto invalid = result;
  invalid.sourceX = std::numeric_limits<double>::quiet_NaN();
  QVERIFY_EXCEPTION_THROWN(encodeInputCutoffMappedResult(invalid), std::invalid_argument);
  invalid = result;
  invalid.orderingContract.clear();
  QVERIFY_EXCEPTION_THROWN(encodeInputCutoffMappedResult(invalid), std::invalid_argument);
  invalid = result;
  invalid.sourceY = invalid.sourceHeight;
  QVERIFY_EXCEPTION_THROWN(encodeInputCutoffMappedResult(invalid), std::invalid_argument);
  QVERIFY_EXCEPTION_THROWN(
      encodeInputCutoffMappedRequest({{result.binding, 250}, {10000001, 0}}), std::invalid_argument
  );
  QVERIFY_EXCEPTION_THROWN(
      encodeInputCutoffReceipt({result.binding, InputCutoffState::DrainObserved}), std::invalid_argument
  );
}

void InputSequenceTests::sealedCutoffCodecSeparatesPrefixAndPoint()
{
  static_assert(!std::is_default_constructible_v<InputCutoffSeal>);
  InputCutoffBinding origin{0x1122334455667788, 19, {31, 400, 1}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17};
  auto next = origin;
  ++next.request;
  ++next.capture.part;
  QVERIFY(continuesInputCutoffSeal(origin, next));
  for (int field = 0; field != 8; ++field) {
    auto changed = next;
    if (field == 0)
      changed.request = origin.request;
    if (field == 1)
      changed.capture = origin.capture;
    if (field == 2)
      ++changed.coreConnectionGeneration;
    if (field == 3)
      ++changed.capture.epoch;
    if (field == 4)
      ++changed.forwarding.generation;
    if (field == 5)
      ++changed.forwarding.focusRevision;
    if (field == 6)
      ++changed.forwarding.geometry;
    if (field == 7)
      ++changed.viewerConnection;
    QVERIFY(!continuesInputCutoffSeal(origin, changed));
  }
  const InputCutoffMappedRequest initial{{origin, 200}, {-10, 27}};
  const auto sealed = encodeInputCutoffSealedRequest(initial);
  QCOMPARE(sealed.substr(0, 4), std::string("DICS"));
  const auto decodedInitial = decodeInputCutoffMappedRequest(std::string_view(sealed).substr(4));
  QVERIFY(decodedInitial && decodedInitial->cutoff.binding == origin && decodedInitial->point == initial.point);
  const auto frame = encodeInputCutoffMapRequest({{{next, 123}, {900, -10}}, origin.request, true});
  QCOMPARE(frame.size(), inputCutoffMapRequestBytes + 4);
  const auto decoded = decodeInputCutoffMapRequest(std::string_view(frame).substr(4));
  QVERIFY(
      decoded && decoded->mapping.cutoff.binding == next && decoded->mapping.cutoff.remainingMs == 123 &&
      decoded->mapping.point == (InputCutoffPoint{900, -10}) && decoded->sealRequest == origin.request &&
      decoded->presentCursor
  );
  for (std::size_t size = 0; size < inputCutoffMapRequestBytes; ++size)
    QVERIFY(!decodeInputCutoffMapRequest(std::string_view(frame).substr(4, size)));
  QVERIFY(!decodeInputCutoffMapRequest(frame.substr(4) + '\0'));
  auto invalid = frame.substr(4);
  invalid.back() = 2;
  QVERIFY(!decodeInputCutoffMapRequest(invalid));
  QVERIFY_EXCEPTION_THROWN(encodeInputCutoffMapRequest({initial, origin.request, false}), std::invalid_argument);
  QVERIFY_EXCEPTION_THROWN(encodeInputCutoffReceipt({origin, InputCutoffState::Sealed}), std::invalid_argument);
  auto receipt = encodeInputCutoffReceipt({origin, InputCutoffState::Denied});
  receipt.back() = static_cast<char>(InputCutoffState::Sealed);
  QVERIFY(!decodeInputCutoffReceipt(std::string_view(receipt).substr(4)));
}

void InputSequenceTests::batchedCutoffCodecBoundsEveryItem()
{
  InputCutoffBinding origin{17, 19, {31, 400, 1}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17};
  InputCutoffBatchRequest request{origin.request};
  InputCutoffBatchResult result{origin.request};
  auto next = origin;
  for (std::size_t index = 0; index < inputCutoffMaximumBatch; ++index) {
    ++next.request;
    ++next.capture.sequence;
    next.capture.part = 1 + index % 3;
    const InputCutoffPoint point{int(index * 7), -int(index * 3)};
    request.items.push_back({{{next, static_cast<std::uint16_t>(100 + index)}, point}, origin.request, index % 2 == 0});
    InputCutoffMappedResult mapped;
    mapped.binding = next;
    mapped.point = point;
    mapped.sourceX = index;
    mapped.sourceY = index * 2;
    mapped.sourceWidth = 1920;
    mapped.sourceHeight = 1080;
    mapped.surfaceIncarnation = 18;
    mapped.viewportRevision = 19;
    mapped.nativeSurface = 20;
    mapped.providerGeneration = 21;
    mapped.nativeRevision = 22;
    mapped.viewerPeerFingerprint.fill(0x7a);
    mapped.orderingContract = "test-reviewed-build:123";
    result.items.push_back(mapped);
  }
  const auto encoded = encodeInputCutoffBatchRequest(request);
  QCOMPARE(encoded.size(), 4 + inputCutoffBatchRequestMaximumBytes);
  const auto decoded = decodeInputCutoffBatchRequest(std::string_view(encoded).substr(4));
  QVERIFY(decoded && decoded->items.size() == inputCutoffMaximumBatch && decoded->sealRequest == origin.request);
  for (std::size_t index = 0; index < inputCutoffMaximumBatch; ++index) {
    QCOMPARE(decoded->items[index].mapping.cutoff.binding, request.items[index].mapping.cutoff.binding);
    QCOMPARE(decoded->items[index].mapping.cutoff.remainingMs, request.items[index].mapping.cutoff.remainingMs);
    QCOMPARE(decoded->items[index].mapping.point, request.items[index].mapping.point);
    QCOMPARE(decoded->items[index].presentCursor, request.items[index].presentCursor);
  }
  const auto reply = encodeInputCutoffBatchResult(result);
  QCOMPARE(reply.size(), 4 + inputCutoffBatchResultMaximumBytes);
  const auto decodedReply = decodeInputCutoffBatchResult(std::string_view(reply).substr(4));
  QVERIFY(decodedReply && decodedReply->items == result.items && decodedReply->sealRequest == result.sealRequest);
  for (std::size_t length = 0; length < inputCutoffBatchRequestMaximumBytes; ++length)
    QVERIFY(!decodeInputCutoffBatchRequest(std::string_view(encoded).substr(4, length)));
  for (std::size_t length = 0; length < inputCutoffBatchResultMaximumBytes; ++length)
    QVERIFY(!decodeInputCutoffBatchResult(std::string_view(reply).substr(4, length)));
  QVERIFY(!decodeInputCutoffBatchRequest(encoded.substr(4) + '\0'));
  QVERIFY(!decodeInputCutoffBatchResult(reply.substr(4) + '\0'));
  for (int invalid = 0; invalid < 8; ++invalid) {
    auto changed = request;
    if (invalid == 0)
      changed.items.clear();
    if (invalid == 1)
      changed.items.push_back(changed.items.back());
    if (invalid == 2)
      changed.items[1].mapping.cutoff.binding = changed.items.front().mapping.cutoff.binding;
    if (invalid == 3)
      ++changed.items[1].mapping.cutoff.binding.forwarding.geometry;
    if (invalid == 4)
      changed.items[1].mapping.cutoff.remainingMs = 0;
    if (invalid == 5)
      changed.items[1].mapping.cutoff.remainingMs = 251;
    if (invalid == 6)
      ++changed.items[1].sealRequest;
    if (invalid == 7)
      changed.sealRequest = changed.items.front().mapping.cutoff.binding.request;
    QVERIFY_EXCEPTION_THROWN(encodeInputCutoffBatchRequest(changed), std::invalid_argument);
  }
  auto mixed = result;
  ++mixed.items[7].viewportRevision;
  QVERIFY_EXCEPTION_THROWN(encodeInputCutoffBatchResult(mixed), std::invalid_argument);
  auto noncanonical = encoded.substr(4);
  noncanonical[inputCutoffBatchHeaderBytes + inputCutoffMappedRequestBytes] = 2;
  QVERIFY(!decodeInputCutoffBatchRequest(noncanonical));
}

void InputSequenceTests::preparationCodecHasNoInventedLease()
{
  static_assert(!std::is_default_constructible_v<PreparedInputFocus>);
  const InputPreparationBinding binding{0x1122334455667788, 19, {31, 400, 3}};
  const InputCutoffPoint point{-10, 27};
  PreparedInputFocusResult focus{binding, point, 17, {11, 12}, 16, 14, 15};
  focus.viewerPeerFingerprint.fill(0x7a);
  InputCutoffBinding cutoff{
      binding.request + 1, 19, binding.capture, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17
  };
  const auto request = encodeInputPreparationRequest({binding, point, 500});
  const auto result = encodePreparedInputFocusResult(focus);
  const auto promotion = encodeInputPreparationPromotion({binding, cutoff, 250});
  const auto cancel = encodeInputPreparationCancellation({binding, 250});
  const auto reply =
      encodeInputPreparationReceipt({binding, InputPreparationState::Released, InputPreparationOutcome::NoTarget});
  QCOMPARE(request.size(), 4 + inputPreparationRequestBytes);
  QCOMPARE(result.size(), 4 + inputPreparationResultBytes);
  QCOMPARE(promotion.size(), 4 + inputPreparationPromotionBytes);
  QCOMPARE(cancel.size(), 4 + inputPreparationCancelBytes);
  QCOMPARE(reply.size(), 4 + inputPreparationReceiptBytes);
  QCOMPARE(decodeInputPreparationRequest(request.substr(4))->binding, binding);
  QCOMPARE(decodeInputPreparationRequest(request.substr(4))->remainingMs, 500);
  QCOMPARE(*decodePreparedInputFocusResult(result.substr(4)), focus);
  QCOMPARE(decodeInputPreparationPromotion(promotion.substr(4))->cutoff, cutoff);
  QCOMPARE(decodeInputPreparationCancellation(cancel.substr(4))->binding, binding);
  QCOMPARE(decodeInputPreparationReceipt(reply.substr(4))->outcome, InputPreparationOutcome::NoTarget);
  const auto check = [](const auto &packet, auto decode) {
    for (std::size_t length = 0; length < packet.size() - 4; ++length)
      if (decode(std::string_view(packet).substr(4, length)))
        return false;
    return !decode(packet.substr(4) + '\0');
  };
  QVERIFY(check(request, decodeInputPreparationRequest));
  QVERIFY(check(result, decodePreparedInputFocusResult));
  QVERIFY(check(promotion, decodeInputPreparationPromotion));
  QVERIFY(check(cancel, decodeInputPreparationCancellation));
  QVERIFY(check(reply, decodeInputPreparationReceipt));
  for (int field = 0; field < 10; ++field) {
    auto changed = cutoff;
    if (field == 0)
      changed.request = binding.request;
    if (field == 1)
      ++changed.coreConnectionGeneration;
    if (field == 2)
      ++changed.capture.sequence;
    if (field == 3)
      ++changed.capture.part;
    if (field == 4)
      ++changed.viewerConnection;
    if (field == 5)
      ++changed.forwarding.owner.epoch;
    if (field == 6)
      ++changed.forwarding.geometry;
    if (field == 7)
      ++changed.forwarding.focusRequest;
    if (field == 8)
      ++changed.forwarding.focusRevision;
    if (field == 9)
      changed.forwarding.scope = ForwardingScope::Keyboard;
    QVERIFY(!matchesInputPreparationPromotion(focus, changed));
  }
  QVERIFY(matchesInputPreparationPromotion(focus, cutoff));
  QVERIFY_EXCEPTION_THROWN(encodeInputPreparationRequest({binding, point, 501}), std::invalid_argument);
  QVERIFY_EXCEPTION_THROWN(encodeInputPreparationCancellation({binding, 251}), std::invalid_argument);
  QVERIFY_EXCEPTION_THROWN(encodeInputPreparationPromotion({binding, cutoff, 251}), std::invalid_argument);
  for (const auto state :
       {InputPreparationState::Preparing, InputPreparationState::Prepared, InputPreparationState::Promoting,
        InputPreparationState::Sealed, InputPreparationState::Canceling}) {
    QVERIFY_EXCEPTION_THROWN(
        encodeInputPreparationReceipt({binding, state, InputPreparationOutcome::None}), std::invalid_argument
    );
    auto forged = reply.substr(4);
    forged[inputPreparationBindingBytes] = static_cast<char>(state);
    QVERIFY(!decodeInputPreparationReceipt(forged));
  }
}

QTEST_GUILESS_MAIN(InputSequenceTests)
