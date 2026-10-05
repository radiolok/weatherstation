"""F0: the application boots on native_sim and answers in the shell."""


def test_boot_and_version(dut):
    kv = dut.shell_kv("ws version")
    assert kv["version"]
    assert kv["board"].startswith("native_sim")
