#!/bin/sh
# monitor_panel.sh - Monitor panel structure changes
# Usage: ./monitor_panel.sh <panel_address_in_hex>

if [ $# -ne 1 ]; then
    echo "Usage: $0 <panel_address_in_hex>"
    echo "Get panel address from: nm /unix | grep panel"
    exit 1
fi

echo "Monitoring panel structure at address 0x$1"
echo "Press Ctrl+C to stop"
echo ""

while true; do
    ./test_panel $1
    echo "---"
    sleep 1
done
