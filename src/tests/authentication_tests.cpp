/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <mesos/executor.hpp>
#include <mesos/resources.hpp>
#include <mesos/scheduler.hpp>

#include <mesos/authentication/authentication.hpp>

#include <process/dispatch.hpp>
#include <process/gmock.hpp>
#include <process/owned.hpp>

#include <stout/nothing.hpp>

#include "tests/mesos.hpp"
#include "tests/utils.hpp"

using namespace process;

using mesos::internal::master::Master;
using mesos::internal::slave::Slave;

using testing::_;
using testing::Eq;
using testing::Return;

namespace mesos {
namespace internal {
namespace tests {


class AuthenticationTest : public MesosTest {};


// This test verifies that an unauthenticated framework is
// denied registration by the master.
TEST_F(AuthenticationTest, UnauthenticatedFramework)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockScheduler sched;
  MesosSchedulerDriver driver(&sched, DEFAULT_FRAMEWORK_INFO, master.get());

  Future<Nothing> error;
  EXPECT_CALL(sched, error(&driver, _))
    .WillOnce(FutureSatisfy(&error));

  driver.start();

  // Scheduler should get error message from the master.
  AWAIT_READY(error);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test verifies that an unauthenticated slave is
// denied registration by the master.
TEST_F(AuthenticationTest, UnauthenticatedSlave)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  Future<ShutdownMessage> shutdownMessage =
    FUTURE_PROTOBUF(ShutdownMessage(), _, _);

  // Start the slave without credentials.
  slave::Flags flags = CreateSlaveFlags();
  flags.credential = None();

  Try<PID<Slave> > slave = StartSlave(flags);
  ASSERT_SOME(slave);

  // Slave should get error message from the master.
  AWAIT_READY(shutdownMessage);
  ASSERT_NE("", shutdownMessage.get().message());

  Shutdown();
}


// This test verifies that when the master is started with framework
// authentication disabled, it registers unauthenticated frameworks.
TEST_F(AuthenticationTest, DisableFrameworkAuthentication)
{
  master::Flags flags = CreateMasterFlags();
  flags.authenticate_frameworks = false; // Disable authentication.

  Try<PID<Master> > master = StartMaster(flags);
  ASSERT_SOME(master);

  // Start the scheduler without credentials.
  MockScheduler sched;
  MesosSchedulerDriver driver(&sched, DEFAULT_FRAMEWORK_INFO, master.get());

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  driver.start();

  // Scheduler should be able to get registered.
  AWAIT_READY(registered);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test verifies that when the master is started with slave
// authentication disabled, it registers unauthenticated slaves.
TEST_F(AuthenticationTest, DisableSlaveAuthentication)
{
  master::Flags flags = CreateMasterFlags();
  flags.authenticate_slaves = false; // Disable authentication.

  Try<PID<Master> > master = StartMaster(flags);
  ASSERT_SOME(master);

  Future<SlaveRegisteredMessage> slaveRegisteredMessage =
    FUTURE_PROTOBUF(SlaveRegisteredMessage(), _, _);

  // Start the slave without credentials.
  slave::Flags slaveFlags = CreateSlaveFlags();
  slaveFlags.credential = None();

  Try<PID<Slave> > slave = StartSlave(slaveFlags);
  ASSERT_SOME(slave);

  // Slave should be able to get registered.
  AWAIT_READY(slaveRegisteredMessage);
  ASSERT_NE("", slaveRegisteredMessage.get().slave_id().value());

  Shutdown();
}


// This test verifies that an authenticated framework is denied
// registration by the master if it uses a different
// FrameworkInfo.principal than Credential.principal.
TEST_F(AuthenticationTest, MismatchedFrameworkInfoPrincipal)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockScheduler sched;
  FrameworkInfo frameworkInfo = DEFAULT_FRAMEWORK_INFO;
  frameworkInfo.set_principal("mismatched-principal");

  MesosSchedulerDriver driver(
      &sched,
      frameworkInfo,
      master.get(),
      DEFAULT_CREDENTIAL);

  Future<Nothing> error;
  EXPECT_CALL(sched, error(&driver, _))
    .WillOnce(FutureSatisfy(&error));

  driver.start();

  // Scheduler should get error message from the master.
  AWAIT_READY(error);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test verifies that an authenticated framework is denied
// registration by the master if it uses a different
// FrameworkInfo::principal than Credential.principal, even
// when authentication is not required.
TEST_F(AuthenticationTest, DisabledFrameworkAuthenticationPrincipalMismatch)
{
  master::Flags flags = CreateMasterFlags();
  flags.authenticate_frameworks = false; // Authentication not required.

  Try<PID<Master> > master = StartMaster(flags);
  ASSERT_SOME(master);

  MockScheduler sched;
  FrameworkInfo frameworkInfo = DEFAULT_FRAMEWORK_INFO;
  frameworkInfo.set_principal("mismatched-principal");

  MesosSchedulerDriver driver(
      &sched,
      frameworkInfo,
      master.get(),
      DEFAULT_CREDENTIAL);

  Future<Nothing> error;
  EXPECT_CALL(sched, error(&driver, _))
    .WillOnce(FutureSatisfy(&error));

  driver.start();

  // Scheduler should get error message from the master.
  AWAIT_READY(error);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test verifies that if a Framework successfully authenticates
// but does not set FrameworkInfo::principal, it is allowed to
// register.
TEST_F(AuthenticationTest, UnspecifiedFrameworkInfoPrincipal)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockScheduler sched;
  FrameworkInfo frameworkInfo = DEFAULT_FRAMEWORK_INFO;
  frameworkInfo.clear_principal();

  MesosSchedulerDriver driver(
      &sched,
      frameworkInfo,
      master.get(),
      DEFAULT_CREDENTIAL);

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  driver.start();

  // Scheduler should be able to get registered.
  AWAIT_READY(registered);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test verifies that when the master is started with
// authentication disabled, it registers authenticated frameworks.
TEST_F(AuthenticationTest, AuthenticatedFramework)
{
  master::Flags flags = CreateMasterFlags();
  flags.authenticate_frameworks = false; // Disable authentication.

  Try<PID<Master> > master = StartMaster(flags);
  ASSERT_SOME(master);

  // Start the scheduler with credentials.
  MockScheduler sched;
  MesosSchedulerDriver driver(
      &sched, DEFAULT_FRAMEWORK_INFO, master.get(), DEFAULT_CREDENTIAL);

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  driver.start();

  // Scheduler should be able to get registered.
  AWAIT_READY(registered);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test verifies that when the master is started with slave
// authentication disabled, it registers authenticated slaves.
TEST_F(AuthenticationTest, AuthenticatedSlave)
{
  master::Flags flags = CreateMasterFlags();
  flags.authenticate_slaves = false; // Disable authentication.

  Try<PID<Master> > master = StartMaster(flags);
  ASSERT_SOME(master);

  Future<SlaveRegisteredMessage> slaveRegisteredMessage =
    FUTURE_PROTOBUF(SlaveRegisteredMessage(), _, _);

  // Start the slave with credentials.
  slave::Flags slaveFlags = CreateSlaveFlags();
  Try<PID<Slave> > slave = StartSlave(slaveFlags);
  ASSERT_SOME(slave);

  // Slave should be able to get registered.
  AWAIT_READY(slaveRegisteredMessage);
  ASSERT_NE("", slaveRegisteredMessage.get().slave_id().value());

  Shutdown();
}


// This test verifies that the framework properly retries
// authentication when authenticate message is lost.
TEST_F(AuthenticationTest, RetryFrameworkAuthentication)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockScheduler sched;
  MesosSchedulerDriver driver(
      &sched, DEFAULT_FRAMEWORK_INFO, master.get(), DEFAULT_CREDENTIAL);

  // Drop the first authenticate message from the scheduler.
  Future<AuthenticateMessage> authenticateMessage =
    DROP_PROTOBUF(AuthenticateMessage(), _, _);

  driver.start();

  AWAIT_READY(authenticateMessage);

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  // Advance the clock for the scheduler to retry.
  Clock::pause();
  Clock::advance(Seconds(5));
  Clock::settle();
  Clock::resume();

  // Scheduler should be able to get registered.
  AWAIT_READY(registered);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test verifies that the slave properly retries
// authentication when authenticate message is lost.
TEST_F(AuthenticationTest, RetrySlaveAuthentication)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  // Drop the first authenticate message from the slave.
  Future<AuthenticateMessage> authenticateMessage =
    DROP_PROTOBUF(AuthenticateMessage(), _, _);

  slave::Flags slaveFlags = CreateSlaveFlags();
  Try<PID<Slave> > slave = StartSlave(slaveFlags);
  ASSERT_SOME(slave);

  AWAIT_READY(authenticateMessage);

  Future<SlaveRegisteredMessage> slaveRegisteredMessage =
    FUTURE_PROTOBUF(SlaveRegisteredMessage(), _, _);

  // Advance the clock for the slave to retry.
  Clock::pause();
  Clock::advance(Seconds(5));
  Clock::settle();
  Clock::resume();

  // Slave should be able to get registered.
  AWAIT_READY(slaveRegisteredMessage);
  ASSERT_NE("", slaveRegisteredMessage.get().slave_id().value());

  Shutdown();
}


// This test verifies that the framework properly retries
// authentication when an intermediate message in SASL protocol
// is lost.
TEST_F(AuthenticationTest, DropIntermediateSASLMessage)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockScheduler sched;
  MesosSchedulerDriver driver(
      &sched, DEFAULT_FRAMEWORK_INFO, master.get(), DEFAULT_CREDENTIAL);

  // Drop the AuthenticationStepMessage from authenticator.
  Future<AuthenticationStepMessage> authenticationStepMessage =
    DROP_PROTOBUF(AuthenticationStepMessage(), _, _);

  driver.start();

  AWAIT_READY(authenticationStepMessage);

  Future<AuthenticationCompletedMessage> authenticationCompletedMessage =
    FUTURE_PROTOBUF(AuthenticationCompletedMessage(), _, _);

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  // Advance the clock for the scheduler to retry.
  Clock::pause();
  Clock::advance(Seconds(5));
  Clock::settle();
  Clock::resume();

  // Ensure another authentication attempt was made.
  AWAIT_READY(authenticationCompletedMessage);

  // Scheduler should be able to get registered.
  AWAIT_READY(registered);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test verifies that the slave properly retries
// authentication when an intermediate message in SASL protocol
// is lost.
TEST_F(AuthenticationTest, DropIntermediateSASLMessageForSlave)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  // Drop the AuthenticationStepMessage from authenticator.
  Future<AuthenticationStepMessage> authenticationStepMessage =
    DROP_PROTOBUF(AuthenticationStepMessage(), _, _);

  slave::Flags slaveFlags = CreateSlaveFlags();
  Try<PID<Slave> > slave = StartSlave(slaveFlags);
  ASSERT_SOME(slave);

  AWAIT_READY(authenticationStepMessage);

  Future<AuthenticationCompletedMessage> authenticationCompletedMessage =
    FUTURE_PROTOBUF(AuthenticationCompletedMessage(), _, _);

  Future<SlaveRegisteredMessage> slaveRegisteredMessage =
    FUTURE_PROTOBUF(SlaveRegisteredMessage(), _, _);

  // Advance the clock for the slave to retry.
  Clock::pause();
  Clock::advance(Seconds(5));
  Clock::settle();
  Clock::resume();

  // Ensure another authentication attempt was made.
  AWAIT_READY(authenticationCompletedMessage);

  // Slave should be able to get registered.
  AWAIT_READY(slaveRegisteredMessage);
  ASSERT_NE("", slaveRegisteredMessage.get().slave_id().value());

  Shutdown();
}


// This test verifies that the framework properly retries
// authentication when the final message in SASL protocol
// is lost. The dropped message causes the master to think
// the framework is authenticated but the framework to think
// otherwise. The framework should retry authentication and
// eventually register.
TEST_F(AuthenticationTest, DropFinalSASLMessage)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockScheduler sched;
  MesosSchedulerDriver driver(
      &sched, DEFAULT_FRAMEWORK_INFO, master.get(), DEFAULT_CREDENTIAL);

  // Drop the AuthenticationCompletedMessage from authenticator.
  Future<AuthenticationCompletedMessage> authenticationCompletedMessage =
    DROP_PROTOBUF(AuthenticationCompletedMessage(), _, _);

  driver.start();

  AWAIT_READY(authenticationCompletedMessage);

  authenticationCompletedMessage =
    FUTURE_PROTOBUF(AuthenticationCompletedMessage(), _, _);

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  // Advance the clock for the scheduler to retry.
  Clock::pause();
  Clock::advance(Seconds(5));
  Clock::settle();
  Clock::resume();

  // Ensure another authentication attempt was made.
  AWAIT_READY(authenticationCompletedMessage);

  // Scheduler should be able to get registered.
  AWAIT_READY(registered);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test verifies that the slave properly retries
// authentication when the final message in SASL protocol
// is lost. The dropped message causes the master to think
// the slave is authenticated but the slave to think
// otherwise. The slave should retry authentication and
// eventually register.
TEST_F(AuthenticationTest, DropFinalSASLMessageForSlave)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  // Drop the AuthenticationCompletedMessage from authenticator.
  Future<AuthenticationCompletedMessage> authenticationCompletedMessage =
    DROP_PROTOBUF(AuthenticationCompletedMessage(), _, _);

  slave::Flags slaveFlags = CreateSlaveFlags();
  Try<PID<Slave> > slave = StartSlave(slaveFlags);
  ASSERT_SOME(slave);

  AWAIT_READY(authenticationCompletedMessage);

  authenticationCompletedMessage =
    FUTURE_PROTOBUF(AuthenticationCompletedMessage(), _, _);

  Future<SlaveRegisteredMessage> slaveRegisteredMessage =
    FUTURE_PROTOBUF(SlaveRegisteredMessage(), _, _);

  // Advance the clock for the scheduler to retry.
  Clock::pause();
  Clock::advance(Seconds(5));
  Clock::settle();
  Clock::resume();

  // Ensure another authentication attempt was made.
  AWAIT_READY(authenticationCompletedMessage);

  // Slave should be able to get registered.
  AWAIT_READY(slaveRegisteredMessage);
  ASSERT_NE("", slaveRegisteredMessage.get().slave_id().value());

  Shutdown();
}


// This test verifies that when a master fails over while a framework
// authentication attempt is in progress the framework properly
// authenticates.
TEST_F(AuthenticationTest, MasterFailover)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockScheduler sched;
  Owned<StandaloneMasterDetector> detector(
      new StandaloneMasterDetector(master.get()));
  TestingMesosSchedulerDriver driver(&sched, detector.get());

  // Drop the authenticate message from the scheduler.
  Future<AuthenticateMessage> authenticateMessage =
    DROP_PROTOBUF(AuthenticateMessage(), _, _);

  driver.start();

  AWAIT_READY(authenticateMessage);

  // While the authentication is in progress simulate a failed over
  // master by restarting the master.
  Stop(master.get());
  master = StartMaster();
  ASSERT_SOME(master);

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  // Appoint a new master and inform the scheduler about it.
  detector->appoint(master.get());

  // Scheduler should successfully register with the new master.
  AWAIT_READY(registered);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test verifies that when a master fails over while a slave
// authentication attempt is in progress the slave properly
// authenticates.
TEST_F(AuthenticationTest, MasterFailoverDuringSlaveAuthentication)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  // Drop the authenticate message from the slave.
  Future<AuthenticateMessage> authenticateMessage =
    DROP_PROTOBUF(AuthenticateMessage(), _, _);

  StandaloneMasterDetector detector(master.get());
  slave::Flags slaveFlags = CreateSlaveFlags();
  Try<PID<Slave> > slave = StartSlave(&detector, slaveFlags);
  ASSERT_SOME(slave);

  AWAIT_READY(authenticateMessage);

  // While the authentication is in progress simulate a failed over
  // master by restarting the master.
  Stop(master.get());
  master = StartMaster();
  ASSERT_SOME(master);

  Future<SlaveRegisteredMessage> slaveRegisteredMessage =
    FUTURE_PROTOBUF(SlaveRegisteredMessage(), _, _);

  // Appoint a new master and inform the slave about it.
  detector.appoint(master.get());

  // Slave should be able to get registered.
  AWAIT_READY(slaveRegisteredMessage);
  ASSERT_NE("", slaveRegisteredMessage.get().slave_id().value());

  Shutdown();
}


// This test verifies that if the scheduler retries authentication
// before the original authentication finishes (e.g., new master
// detected due to leader election), it is handled properly.
TEST_F(AuthenticationTest, LeaderElection)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockScheduler sched;
  Owned<StandaloneMasterDetector> detector(
      new StandaloneMasterDetector(master.get()));
  TestingMesosSchedulerDriver driver(&sched, detector.get());

  // Drop the AuthenticationStepMessage from authenticator.
  Future<AuthenticationStepMessage> authenticationStepMessage =
    DROP_PROTOBUF(AuthenticationStepMessage(), _, _);

  driver.start();

  // Drop the intermediate SASL message so that authentication fails.
  AWAIT_READY(authenticationStepMessage);

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  // Appoint a new master and inform the scheduler about it.
  detector->appoint(master.get());

  // Scheduler should successfully register with the new master.
  AWAIT_READY(registered);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test verifies that if the slave retries authentication
// before the original authentication finishes (e.g., new master
// detected due to leader election), it is handled properly.
TEST_F(AuthenticationTest, LeaderElectionDuringSlaveAuthentication)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  // Drop the AuthenticationStepMessage from authenticator.
  Future<AuthenticationStepMessage> authenticationStepMessage =
    DROP_PROTOBUF(AuthenticationStepMessage(), _, _);

  StandaloneMasterDetector detector(master.get());
  slave::Flags slaveFlags = CreateSlaveFlags();
  Try<PID<Slave> > slave = StartSlave(&detector, slaveFlags);
  ASSERT_SOME(slave);

  // Drop the intermediate SASL message so that authentication fails.
  AWAIT_READY(authenticationStepMessage);

  Future<SlaveRegisteredMessage> slaveRegisteredMessage =
    FUTURE_PROTOBUF(SlaveRegisteredMessage(), _, _);

  // Appoint a new master and inform the slave about it.
  detector.appoint(master.get());

  // Slave should be able to get registered.
  AWAIT_READY(slaveRegisteredMessage);
  ASSERT_NE("", slaveRegisteredMessage.get().slave_id().value());

  Shutdown();
}


// This test verifies that if a scheduler fails over in the midst of
// authentication it successfully re-authenticates and re-registers
// with the master when it comes back up.
TEST_F(AuthenticationTest, SchedulerFailover)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  // Launch the first (i.e., failing) scheduler.
  MockScheduler sched1;
  Owned<StandaloneMasterDetector> detector(
      new StandaloneMasterDetector(master.get()));
  TestingMesosSchedulerDriver driver1(&sched1, detector.get());

  Future<FrameworkID> frameworkId;
  EXPECT_CALL(sched1, registered(&driver1, _, _))
    .WillOnce(FutureArg<1>(&frameworkId));

  driver1.start();

  AWAIT_READY(frameworkId);

  // Drop the AuthenticationStepMessage from authenticator
  // to stop authentication from succeeding.
  Future<AuthenticationStepMessage> authenticationStepMessage =
    DROP_PROTOBUF(AuthenticationStepMessage(), _, _);

  EXPECT_CALL(sched1, disconnected(&driver1));

  // Appoint a new master and inform the scheduler about it.
  detector->appoint(master.get());

  AWAIT_READY(authenticationStepMessage);

  // Now launch the second (i.e., failover) scheduler using the
  // framework id recorded from the first scheduler and wait until it
  // gets a registered callback.

  MockScheduler sched2;

  FrameworkInfo framework2 = DEFAULT_FRAMEWORK_INFO;
  framework2.mutable_id()->MergeFrom(frameworkId.get());

  MesosSchedulerDriver driver2(
      &sched2, framework2, master.get(), DEFAULT_CREDENTIAL);

  Future<Nothing> sched2Registered;
  EXPECT_CALL(sched2, registered(&driver2, frameworkId.get(), _))
    .WillOnce(FutureSatisfy(&sched2Registered));

  Future<Nothing> sched1Error;
  EXPECT_CALL(sched1, error(&driver1, "Framework failed over"))
    .WillOnce(FutureSatisfy(&sched1Error));

  driver2.start();

  AWAIT_READY(sched2Registered);
  AWAIT_READY(sched1Error);

  driver2.stop();
  driver2.join();

  driver1.stop();
  driver1.join();

  Shutdown();
}


// This test verifies that a scheduler's re-registration will be
// rejected if it specifies a principal different from what's used in
// authentication.
TEST_F(AuthenticationTest, RejectedSchedulerFailover)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  // Launch the first scheduler.
  MockScheduler sched1;
  Owned<StandaloneMasterDetector> detector(
      new StandaloneMasterDetector(master.get()));
  TestingMesosSchedulerDriver driver1(&sched1, detector.get());

  Future<FrameworkID> frameworkId;
  EXPECT_CALL(sched1, registered(&driver1, _, _))
    .WillOnce(FutureArg<1>(&frameworkId));

  driver1.start();

  AWAIT_READY(frameworkId);

  // Drop the AuthenticationStepMessage from authenticator
  // to stop authentication from succeeding.
  Future<AuthenticationStepMessage> authenticationStepMessage =
    DROP_PROTOBUF(AuthenticationStepMessage(), _, _);

  EXPECT_CALL(sched1, disconnected(&driver1));

  // Appoint a new master and inform the scheduler about it.
  detector->appoint(master.get());

  AWAIT_READY(authenticationStepMessage);

  // Attempt to failover to scheduler 2 while scheduler 1 is still
  // up. We use the framework id recorded from scheduler 1 but change
  // the principal in FrameworInfo and it will be denied. Scheduler 1
  // will not be asked to shutdown.
  MockScheduler sched2;

  FrameworkInfo framework2 = DEFAULT_FRAMEWORK_INFO;
  framework2.mutable_id()->MergeFrom(frameworkId.get());
  framework2.set_principal("mismatched-principal");

  MesosSchedulerDriver driver2(
      &sched2, framework2, master.get(), DEFAULT_CREDENTIAL);

  Future<Nothing> sched1Error;
  EXPECT_CALL(sched1, error(&driver1, _))
    .Times(0);

  Future<Nothing> sched2Error;
  EXPECT_CALL(sched2, error(&driver2, _))
    .WillOnce(FutureSatisfy(&sched2Error));

  driver2.start();

  // Scheduler 2 should get error message from the master.
  AWAIT_READY(sched2Error);

  driver2.stop();
  driver2.join();

  driver1.stop();
  driver1.join();

  Shutdown();
}

} // namespace tests {
} // namespace internal {
} // namespace mesos {
