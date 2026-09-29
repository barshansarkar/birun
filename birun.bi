// birun.bi - Phase 2: bi-powered with logic

let MODE = env("MODE", "release")

route TASK "/build" {
    desc("Build the app (" + MODE + ")")
    if (MODE == "debug") {
        run("echo debug build")
    } else {
        run("echo release build")
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
