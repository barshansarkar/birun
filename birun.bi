// birun.bi — Phase 3: caching + safe shell

let MODE = env("MODE", "release")

route TASK "/build" {
    desc("Build the app (" + MODE + ")")
    inputs("birun.bi", "src/**/*.cpp", "src/**/*.hpp")
    outputs("out/birun")
    if (MODE == "debug") {
        run("mkdir -p out && echo debug > out/birun")
    } else {
        run("mkdir -p out && echo release > out/birun")
    }
}

route TASK "/test" {
    desc("Run tests")
    depends("build")
    run("echo running tests...")
}

route TASK "/all" {
    desc("Run everything")
    depends("build")
    depends("test")
}

route TASK "/clean" {
    desc("Cleanup")
    for dir in ["build", "dist"] {
        run("echo cleaning " + dir)
    }
}

// ---- Injection safety test (runs only when task called) ----
route TASK "/inject" {
    desc("Verify glob() is not shell-evaluated")
    let n = len(glob("/etc/passwd; echo HACKED"))
    run("echo 'glob expanded to " + str(n) + " items (expect 0)'")
    if (n == 0) {
        run("echo '✓ injection blocked'")
    } else {
        run("echo '✗ INJECTION DETECTED'")
    }
}

// ---- shFull 100x leak test ----
route TASK "/shtest" {
    desc("Exercise shFull 100 times")
    run("rm -f /tmp/.birun-err-*")
    for i in range(100) {
        let r = shFull("echo line " + str(i))
        // discard result — no print
    }
    run("echo -n 'leaked /tmp files: '; ls /tmp/.birun-err-* 2>/dev/null | wc -l")
}
