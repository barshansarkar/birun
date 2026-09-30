// ============================================================
//  benchmark.birun.bi — demo for `birun -jN` scaling
//
//  Usage (from the examples/ directory):
//    birun -f benchmark.birun.bi --list
//    time birun -f benchmark.birun.bi -j1 all
//    time birun -f benchmark.birun.bi -j4 all
//    time birun -f benchmark.birun.bi -j8 all
//
//  Each leaf task sleeps 1s. `all` depends on 8 leaves.
//  Expected wall-clock time (roughly):
//    -j1  ~8.0s
//    -j2  ~4.0s
//    -j4  ~2.0s
//    -j8  ~1.0s
//
//  The cache is intentionally left OFF here: no inputs()/outputs()
//  means each run always re-executes. Use `--no-cache` too if you
//  want to be explicit.
// ============================================================

route TASK "/a" { desc("sleep 1s"); run("sleep 1") }
route TASK "/b" { desc("sleep 1s"); run("sleep 1") }
route TASK "/c" { desc("sleep 1s"); run("sleep 1") }
route TASK "/d" { desc("sleep 1s"); run("sleep 1") }
route TASK "/e" { desc("sleep 1s"); run("sleep 1") }
route TASK "/f" { desc("sleep 1s"); run("sleep 1") }
route TASK "/g" { desc("sleep 1s"); run("sleep 1") }
route TASK "/h" { desc("sleep 1s"); run("sleep 1") }

route TASK "/all" {
    desc("run all 8 leaves (parallel demo)")
    depends("a", "b", "c", "d", "e", "f", "g", "h")
}