#
#    Copyright (c) 2021 Project CHIP Authors
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

import logging

from .library_handle import _GetLoggingLibraryHandle
from .types import LogRedirectCallback_t

# Defines match src/lib/support/logging/Constants.h (LogCategory enum)
LOG_CATEGORY_NONE = 0
LOG_CATEGORY_ERROR = 1
LOG_CATEGORY_PROGRESS = 2
LOG_CATEGORY_DETAIL = 3
LOG_CATEGORY_AUTOMATION = 4


@LogRedirectCallback_t
def _RedirectToPythonLogging(category, module, message):

    module = module.decode('utf-8')
    message = message.decode('utf-8')

    logger = logging.getLogger('chip.native.%s' % module)

    if category == LOG_CATEGORY_ERROR:
        logger.error("%s", message)
    elif category == LOG_CATEGORY_PROGRESS:
        logger.info("%s", message)
    elif category in (LOG_CATEGORY_DETAIL, LOG_CATEGORY_AUTOMATION):
        logger.debug("%s", message)
    else:
        # All logs are expected to have some reasonable category. This treats
        # unknonw/None as critical.
        logging.critical("%s", message)


def RedirectToPythonLogging():
    """Redireects CHIP logging to python logging module."""

    handle = _GetLoggingLibraryHandle()
    handle.pychip_logging_set_callback(_RedirectToPythonLogging)


def SetLogFilter(category):
    if category < 0 or category > pow(2, 8):
        raise ValueError("category must be an unsigned 8-bit integer")

    handle = _GetLoggingLibraryHandle()
    handle.pychip_logging_SetLogFilter(category)


def GetLogFilter():
    handle = _GetLoggingLibraryHandle()
    return handle.pychip_logging_GetLogFilter()
