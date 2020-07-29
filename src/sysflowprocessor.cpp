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

#include "sysflowprocessor.h"

using sysflowprocessor::SysFlowProcessor;

CREATE_LOGGER(SysFlowProcessor, "sysflow.sysflowprocessor");

SysFlowProcessor::SysFlowProcessor(context::SysFlowContext *cxt)
    : m_exit(false) {
  m_cxt = cxt;
  time_t start = 0;
  if (m_cxt->getFileDuration() > 0) {
    start = utils::getCurrentTime(m_cxt);
  }
  if (m_cxt->isStatsEnabled()) {
    m_statsTime = utils::getCurrentTime(m_cxt);
  } else {
    m_statsTime = 0;
  }
  if (!m_cxt->isDomainSock()) {
    m_writer = new writer::SFFileWriter(cxt, start);
  } else {
    m_writer = new writer::SFSocketWriter(cxt, start);
  }
  m_containerCxt = new container::ContainerContext(m_cxt, m_writer);
  m_fileCxt = new file::FileContext(m_containerCxt, m_writer);
  m_processCxt =
      new process::ProcessContext(m_cxt, m_containerCxt, m_fileCxt, m_writer);
  m_dfPrcr =
      new dataflow::DataFlowProcessor(m_cxt, m_writer, m_processCxt, m_fileCxt);
  m_ctrlPrcr = new controlflow::ControlFlowProcessor(m_cxt, m_writer,
                                                     m_processCxt, m_dfPrcr);
  m_cxt->init(m_processCxt, m_fileCxt, m_containerCxt);
}

SysFlowProcessor::~SysFlowProcessor() {
  delete m_dfPrcr;
  delete m_ctrlPrcr;
  delete m_containerCxt;
  delete m_processCxt;
  delete m_fileCxt;
  delete m_writer;
  delete m_cxt;
}

void SysFlowProcessor::clearTables() {
  m_processCxt->clearProcesses();
  m_containerCxt->clearContainers();
  m_fileCxt->clearFiles();
}

bool SysFlowProcessor::checkAndRotateFile() {
  bool fileRotated = false;
  time_t curTime = utils::getCurrentTime(m_cxt);
  if (m_writer->isExpired(curTime)) {
    SF_INFO(m_logger,
            "Container Table: " << m_containerCxt->getSize()
                                << " Process Table: " << m_processCxt->getSize()
                                << " NetworkFlow Table: " << m_dfPrcr->getSize()
                                << " Num Records Written: "
                                << m_writer->getNumRecs());
    m_writer->reset(curTime);
    clearTables();
    fileRotated = true;
  }
  if (m_statsTime > 0) {
    double duration = difftime(curTime, m_statsTime);
    if (duration >= m_cxt->getStatsInterval()) {
      m_dfPrcr->printFlowStats();
      m_statsTime = curTime;
    }
  }
  return fileRotated;
}

int SysFlowProcessor::checkForExpiredRecords() {
  int numExpired = m_dfPrcr->checkForExpiredRecords();
  if (numExpired) {
    SF_DEBUG(m_logger, "Data Flow Records exported: " << numExpired);
  }
  int numProcExpired = m_ctrlPrcr->checkForExpiredRecords();
  if (numProcExpired) {
    SF_DEBUG(m_logger, "Data Flow Records exported: " << numProcExpired);
  }
  return numExpired + numProcExpired;
}

int SysFlowProcessor::run() {
  int32_t res = 0;
  api::SysFlowInspector *inspector = m_cxt->getInspector();
  api::SysFlowEvent *ev = nullptr;
  try {
    m_writer->initialize();
    while (true) {
      res = inspector->next(&ev);
      //printf("Record res: %d, opflag: %d\n", res, ev->opFlag);
      if (res == SF_TIMEOUT) {
        if (m_exit) {
          break;
        }
        checkForExpiredRecords();
        m_processCxt->checkForDeletion();
        checkAndRotateFile();
        continue;
      } else if (res == SF_EOF) {
        break;
      } else if (res != SF_SUCCESS) {
        SF_ERROR(m_logger, "System event processor failed with res = "
                               << res << " and error: "
                               << m_cxt->getInspector()->getLastError());
        throw std::system_error(EIO, std::system_category(),  m_cxt->getInspector()->getLastError());
      }
      m_cxt->timeStamp = ev->getTS();
      if (m_exit) {
        break;
      }
      checkForExpiredRecords();
      m_processCxt->checkForDeletion();
      checkAndRotateFile();
      if (m_cxt->isFilterContainers() && !ev->isInContainer()) {
        continue;
      }
      switch (ev->opFlag) {
        SF_EXECVE_EXIT(ev)
        SF_CLONE_EXIT(ev)
        SF_PROCEXIT_E_X(ev)
        SF_OPEN_EXIT(ev)
        SF_ACCEPT_EXIT(ev)
        SF_CONNECT_EXIT(ev)
        SF_SEND_EXIT(ev)
        SF_RECV_EXIT(ev)
        SF_CLOSE_EXIT(ev)
        SF_SETNS_EXIT(ev)
        SF_MKDIR_EXIT(ev)
        SF_RMDIR_EXIT(ev)
        SF_LINK_EXIT(ev)
        SF_UNLINK_EXIT(ev)
        SF_SYMLINK_EXIT(ev)
        SF_RENAME_EXIT(ev)
        SF_SETUID(ev)
        SF_SHUTDOWN_EXIT(ev)
        SF_MMAP_EXIT(ev)
      }
    }
    SF_INFO(m_logger, "Exiting scap loop... shutting down");
    SF_INFO(m_logger,
            "Container Table: " << m_containerCxt->getSize()
                                << " Process Table: " << m_processCxt->getSize()
                                << " NetworkFlow Table: " << m_dfPrcr->getSize()
                                << " Num Records Written: "
                                << m_writer->getNumRecs());
  } catch (std::system_error &e) {
    SF_ERROR(m_logger, "Sysdig exception " << e.what());
    return 1;
  } /*catch (avro::Exception &ex) {
    SF_ERROR(m_logger, "Avro Exception! Error: " << ex.what());
    return 1;
  }*/
  return 0;
}
