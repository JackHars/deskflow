/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "InputSequenceTests.h"
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

QTEST_GUILESS_MAIN(InputSequenceTests)
