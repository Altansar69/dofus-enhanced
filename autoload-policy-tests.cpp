#include "autoload-policy.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        using D = AutoLoadPolicy::Decision;
        auto check = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
        AutoLoadPolicy policy(100);
        check(policy.Observe(5, 99, 0, true, true, true) == D::Ignore, "Existing client must be ignored");
        check(policy.Observe(5, 100, 60000, true, true, true) == D::Ignore, "Cutoff identity must remain ignored");
        check(policy.Observe(5, 101, 60000, true, true, true) == D::Wait, "Reused PID/new identity needs grace");
        check(policy.Observe(5, 101, 84999, true, true, true) == D::Wait, "Grace must not end early");
        check(policy.Observe(5, 101, 85000, true, false, true) == D::Wait, "Missing modules must prevent load");
        check(policy.Observe(5, 101, 85000, true, true, false) == D::Disabled, "Disabled watcher must not load");
        check(policy.Observe(5, 101, 85000, false, true, true) == D::Ignore, "Exited process must not load");
        check(policy.Observe(5, 101, 85000, true, true, true) == D::Attempt, "Ready new identity should load once");
        check(policy.Observe(5, 101, 86000, true, true, true) == D::Ignore, "Attempt must never repeat after failure");
        check(policy.Observe(5, 102, 90000, true, true, true) == D::Wait, "Second PID reuse must have separate state");
        check(policy.Observe(5, 102, 115000, true, true, true) == D::Attempt, "New process may load once");
        check(policy.Observe(6, 103, 0, true, false, true) == D::Wait, "Pending process should wait");
        check(policy.Observe(6, 103, 180000, true, true, true) == D::Expired, "Deadline must prevent late load");
        check(policy.Observe(6, 103, 181000, true, true, true) == D::Ignore, "Expired process must not retry");
        check(policy.Observe(0, 104, 0, true, true, true) == D::Ignore, "Invalid identity must not load");
        std::cout << "PASS: baseline, grace, readiness, disable, exit, PID reuse, deadline, one attempt\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
