import 'package:flutter_test/flutter_test.dart';

/// M0 阶段只占位：真正的测试等 M1 接上 `InstallerEngine` 后再写
/// （那时才谈得上"解压到临时目录对不对""注册表键值对不对"这种可断言的东西）。
void main() {
  test('安装器工程占位', () {
    expect(1 + 1, 2);
  });
}
