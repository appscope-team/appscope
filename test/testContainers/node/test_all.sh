#!/bin/bash

ERR=0

export SCOPE_EVENT_DEST=file:///opt/test/logs/events.log
/opt/test/bin/test_node.sh
ERR+=$?

exit ${ERR}
