/** Copyright (C) 2019 IBM Corporation.
 *
 * Authors:
 * Frederico Araujo <frederico.araujo@ibm.com>
 * Teryl Taylor <terylt@ibm.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#include "processeventprocessor.h"

using processevent::ProcessEventProcessor;

CREATE_LOGGER(ProcessEventProcessor, "sysflow.processevent");
ProcessEventProcessor::ProcessEventProcessor(
    writer::SysFlowWriter *writer, process::ProcessContext *pc,
    dataflow::DataFlowProcessor *dfPrcr) {
  m_processCxt = pc;
  m_writer = writer;
  m_dfPrcr = dfPrcr;
}

ProcessEventProcessor::~ProcessEventProcessor() = default;

void ProcessEventProcessor::setUID(api::SysFlowEvent *ev) {
  m_uid = ev->getUIDFromParameter();
}

void ProcessEventProcessor::writeCloneEvent(api::SysFlowEvent *ev) {
  bool created = false;
  ProcessObj *proc =
      m_processCxt->getProcess(ev, SFObjectState::CREATED, created);
  m_procEvt.opFlags = OP_CLONE;
  m_procEvt.ts = ev->getTS();
  m_procEvt.procOID.hpid = proc->proc.oid.hpid;
  m_procEvt.procOID.createTS = proc->proc.oid.createTS;
  m_procEvt.tid = ev->getTID();
  m_procEvt.ret = ev->getSysCallResult();
  m_procEvt.args.clear();
  m_writer->writeProcessEvent(&m_procEvt);
}

void ProcessEventProcessor::writeSetUIDEvent(api::SysFlowEvent *ev) {
  bool created = false;
  ProcessObj *proc = m_processCxt->getProcess(ev, SFObjectState::REUP, created);
  m_procEvt.opFlags = OP_SETUID;
  m_procEvt.ts = ev->getTS();
  m_procEvt.procOID.hpid = proc->proc.oid.hpid;
  m_procEvt.procOID.createTS = proc->proc.oid.createTS;
  m_procEvt.tid = ev->getTID();
  m_procEvt.ret = ev->getSysCallResult();
  m_procEvt.args.clear();
  m_procEvt.args.push_back(m_uid);
  m_writer->writeProcessEvent(&m_procEvt);
  m_procEvt.args.clear();
}

void ProcessEventProcessor::writeExitEvent(api::SysFlowEvent *ev) {
  bool created = false;
  ProcessObj *proc = m_processCxt->getProcess(ev, SFObjectState::REUP, created);
  m_procEvt.opFlags = OP_EXIT;
  m_procEvt.ts = ev->getTS();
  m_procEvt.procOID.hpid = proc->proc.oid.hpid;
  m_procEvt.procOID.createTS = proc->proc.oid.createTS;
  m_procEvt.tid = ev->getTID();
  m_procEvt.ret = ev->getSysCallResult();
  m_procEvt.args.clear();
  int64_t tid = -1;
  if (!ev->isMainThread()) {
    tid = ev->getTID();
  }
  m_dfPrcr->removeAndWriteDFFromProc(proc, tid);
  m_writer->writeProcessEvent(&m_procEvt);
  // delete the process from the proc table after an exit
  if (ev->isMainThread()) {
    // m_processCxt->deleteProcess(&proc);
    m_processCxt->markForDeletion(&proc);
  }
}

void ProcessEventProcessor::writeExecEvent(api::SysFlowEvent *ev) {
  bool created = false;
  ProcessObj *proc =
      m_processCxt->getProcess(ev, SFObjectState::CREATED, created);

  // If Clones are filtered out (or a process like bash is filtered out) then we
  // will only see the EXEC of this process, and the getProcess above will
  // actually create it.  So the question is do we want to add another process
  // record just to mark it modified at this point?
  if (!created) {
    m_processCxt->updateProcess(&(proc->proc), ev, SFObjectState::MODIFIED);
    SF_DEBUG(m_logger, "Writing modified process..." << proc->proc.exe);
    m_writer->writeProcess(&(proc->proc));
  }

  m_procEvt.opFlags = OP_EXEC;
  m_procEvt.ts = ev->getTS();
  m_procEvt.procOID.hpid = proc->proc.oid.hpid;
  m_procEvt.procOID.createTS = proc->proc.oid.createTS;
  m_procEvt.tid = ev->getTID();
  m_procEvt.ret = ev->getSysCallResult();
  m_procEvt.args.clear();
  m_writer->writeProcessEvent(&m_procEvt);
}
