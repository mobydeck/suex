#!/bin/bash
# Test script for suex utility

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Path to suex binary
SUEX_BIN="./suex"

# Test counter
TOTAL_TESTS=0
PASSED_TESTS=0

# -----------------------------------------------------
# Helper functions
# -----------------------------------------------------

# Function to run a test and check result
run_test() {
    local test_name="$1"
    local test_cmd="$2"
    local expected_exit="$3"
    local expected_output="$4"
    local test_description="$5"

    TOTAL_TESTS=$((TOTAL_TESTS+1))

    echo -e "${YELLOW}Test #$TOTAL_TESTS: $test_name${NC}"
    echo "Description: $test_description"
    echo "Command: $test_cmd"

    # Run the command and capture output and exit code
    output=$(eval "$test_cmd" 2>&1)
    exit_code=$?

    # Check exit code
    if [[ $exit_code -eq $expected_exit ]]; then
        exit_result="✓"
    else
        exit_result="✗"
    fi

    # Check output if expected_output is provided
    if [[ -n "$expected_output" ]]; then
        if echo "$output" | grep -q "$expected_output"; then
            output_result="✓"
        else
            output_result="✗"
        fi
    else
        output_result="✓" # Skip output check if no expected output
    fi

    # Display results
    echo "Output: $output"
    echo "Exit code: $exit_code (expected: $expected_exit) $exit_result"

    if [[ -n "$expected_output" ]]; then
        echo "Output check (contains '$expected_output'): $output_result"
    fi

    # Determine if test passed
    if [[ "$exit_result" == "✓" && "$output_result" == "✓" ]]; then
        echo -e "${GREEN}Test PASSED${NC}"
        PASSED_TESTS=$((PASSED_TESTS+1))
    else
        echo -e "${RED}Test FAILED${NC}"
    fi

    echo "---------------------------------------------"
}

require_root() {
    if [[ $(id -u) -ne 0 ]]; then
        echo -e "${RED}Error: This test script requires root privileges${NC}"
        echo "Please run with sudo or as root"
        exit 1
    fi
}

# Check if suex binary exists and is executable
check_suex_binary() {
    if [[ ! -x "$SUEX_BIN" ]]; then
        echo -e "${RED}Error: suex binary not found or not executable at $SUEX_BIN${NC}"
        exit 1
    fi
}

# Create a test user if it doesn't exist
create_test_user() {
    local username="$1"

    if ! id -u "$username" &>/dev/null; then
        useradd -m "$username"
        echo "$username:password" | chpasswd
        echo "Created test user: $username"
    else
        echo "Using existing user: $username"
    fi
}

# Create a test group if it doesn't exist
create_test_group() {
    local groupname="$1"

    if ! getent group "$groupname" &>/dev/null; then
        groupadd "$groupname"
        echo "Created test group: $groupname"
    else
        echo "Using existing group: $groupname"
    fi
}

# Clean up test users/groups after tests
cleanup() {
    echo "Cleaning up test users..."
    userdel -r suextest 2>/dev/null || true
    groupdel suexgroup 2>/dev/null || true
}

# -----------------------------------------------------
# Setup
# -----------------------------------------------------
require_root
check_suex_binary
create_test_user "suextest"
create_test_group "suexgroup"

# Add suextest to suexgroup
usermod -a -G suexgroup suextest

# Ensure suex has proper permissions
chown root:root "$SUEX_BIN"
chmod u+s "$SUEX_BIN"

# Create suex group for permission testing
create_test_group "suex"

echo -e "${YELLOW}Setting up test environment...${NC}"
echo "Making sure $SUEX_BIN is setuid-root"
ls -la "$SUEX_BIN"
echo "---------------------------------------------"

# -----------------------------------------------------
# Basic functionality tests
# -----------------------------------------------------

# Basic command execution
run_test "Basic execution" \
    "$SUEX_BIN root whoami" \
    0 "root" \
    "Execute whoami as root"

# User switching
run_test "User switch" \
    "$SUEX_BIN suextest whoami" \
    0 "suextest" \
    "Execute whoami as suextest user"

# User and group switching
run_test "User+Group switch" \
    "$SUEX_BIN suextest:suexgroup id -ng" \
    0 "suexgroup" \
    "Execute id command with specific group"

# Using numeric uid
run_test "Numeric UID" \
    "suextest_uid=\$(id -u suextest) && $SUEX_BIN \$suextest_uid whoami" \
    0 "suextest" \
    "Execute command with numeric user ID"

# Using numeric uid and gid
run_test "Numeric UID:GID" \
    "suextest_uid=\$(id -u suextest) && suexgroup_gid=\$(getent group suexgroup | cut -d: -f3) && \
     $SUEX_BIN \$suextest_uid:\$suexgroup_gid id -ng" \
    0 "suexgroup" \
    "Execute command with numeric user ID and group ID"

# Environment passing
run_test "Environment passing" \
    "TEST_ENV='hello world' $SUEX_BIN suextest bash -c 'echo \$TEST_ENV'" \
    0 "hello world" \
    "Verify environment variables are passed to executed command"

# -----------------------------------------------------
# Error handling tests
# -----------------------------------------------------

# Invalid user
run_test "Invalid user" \
    "$SUEX_BIN nonexistentuser whoami" \
    1 "Failed to find user" \
    "Try to execute command as non-existent user"

# Invalid group
run_test "Invalid group" \
    "$SUEX_BIN root:nonexistentgroup whoami" \
    1 "Failed to find group" \
    "Try to execute command with non-existent group"

# No command provided
run_test "No command" \
    "$SUEX_BIN root" \
    1 "Usage" \
    "Run suex without specifying a command"

# Invalid command
run_test "Invalid command" \
    "$SUEX_BIN root /bin/nonexistentcommand" \
    127 "No such file or directory" \
    "Try to execute non-existent command"

# -----------------------------------------------------
# Permission tests
# -----------------------------------------------------

# Test non-root running suex

# First try as normal user - should fail
run_test "Non-root execution (not in suex group)" \
    "sudo -u suextest $SUEX_BIN root whoami" \
    1 "Permission denied" \
    "Try to run suex as a user not in the suex group"

# Add suextest to suex group and set permissions
usermod -a -G suex suextest
chown root:suex "$SUEX_BIN"
chmod 4750 "$SUEX_BIN"

run_test "Non-root execution (in suex group) without user" \
    "sudo -u suextest $SUEX_BIN whoami" \
    0 "root" \
    "Run suex as a user in the suex group without user"

run_test "Non-root execution (in suex group)" \
    "sudo -u suextest $SUEX_BIN root whoami" \
    0 "root" \
    "Run suex as a user in the suex group"

run_test "Non-root execution (in suex group) as adm user" \
    "sudo -u suextest $SUEX_BIN adm whoami" \
    0 "adm" \
    "Run suex as a user in the suex group as adm user"

# -----------------------------------------------------
# Signal handling tests
# -----------------------------------------------------

# Create a test script for signal handling that writes its PID to a file
cat > /tmp/signal_test.sh << 'EOF'
#!/bin/bash
trap 'echo "Signal caught"; exit 42' TERM INT

# Echo start message with PID for debugging
echo "Test process started with PID: $$"

# Create a counter file to track progress
echo "0" > /tmp/signal_counter

# Loop and increment counter
for i in {1..30}; do
    curr=$(cat /tmp/signal_counter)
    echo $((curr + 1)) > /tmp/signal_counter
    echo "Counter: $((curr + 1))"
    sleep 1
done

echo "Loop completed without receiving signal"
exit 0
EOF
chmod +x /tmp/signal_test.sh

# Run the test in a way that doesn't depend on capturing PID
run_test "Signal handling" \
    "# Start the process in the background
    $SUEX_BIN suextest /tmp/signal_test.sh > /tmp/signal_output 2>&1 &

    # Sleep briefly to allow the process to start
    sleep 2

    # Get current counter value
    starting_count=$(cat /tmp/signal_counter 2>/dev/null || echo 0)
    echo \"Starting count: $starting_count\"

    # Find all signal_test.sh processes and kill them with SIGTERM
    pkill -f '/tmp/signal_test.sh'

    # Wait a moment for the signal handler to execute
    sleep 2

    # Check if 'Signal caught' appears in the output
    if grep -q 'Signal caught' /tmp/signal_output; then
        echo \"Success: Signal was caught by the process\"
        grep 'Signal caught' /tmp/signal_output
        exit 0
    else
        echo \"Failure: Signal was not caught\"
        cat /tmp/signal_output
        exit 1
    fi" \
    0 "Signal caught" \
    "Verify signal handling without relying on PID capture"

# Clean up
rm -f /tmp/signal_test.sh /tmp/signal_output /tmp/signal_counter


# -----------------------------------------------------
# Summary
# -----------------------------------------------------

echo -e "${YELLOW}Test Summary:${NC}"
echo -e "Total tests: $TOTAL_TESTS"
echo -e "Passed tests: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed tests: ${RED}$((TOTAL_TESTS-PASSED_TESTS))${NC}"

# Clean up
cleanup
rm -f /tmp/signal_test.sh

if [[ $PASSED_TESTS -eq $TOTAL_TESTS ]]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi