#!/bin/bash
# fxtk v2.2 源码发布: 打包核心库 + PC 模拟器 + 全部示例(含画布/抗锯齿) + 无头测试 + 文档/许可/CI
VER=v2.2
OUT=fxtk-$VER
rm -rf $OUT fxtk-$VER.tar.gz
mkdir -p $OUT/demo-main/test $OUT/examples/canvas $OUT/docs $OUT/.github/workflows

# 核心库 (纯 C, 布局/绘图/控件/字体/特效/扩展)
cp -r ../components $OUT/

# PC 模拟器: 驱动 + demo app + GPU 光追 (仅源码)
cp *.c *.h *.sh $OUT/demo-main/ 2>/dev/null

# 无头测试/渲染工具 (源码)
cp test/*.c test/*.h $OUT/demo-main/test/ 2>/dev/null

# 顶层示例
EXDIR=examples; [ -d "$EXDIR" ] || EXDIR=../examples
cp "$EXDIR"/*.c $OUT/examples/ 2>/dev/null && echo "✅ 顶层示例: $(ls $OUT/examples/*.c | wc -l) 个"

# 画布教程示例 (含抗锯齿 canvas_07_aa)
cp "$EXDIR"/canvas/*.c $OUT/examples/canvas/ 2>/dev/null && echo "✅ 画布示例: $(ls $OUT/examples/canvas/*.c | wc -l) 个"

# 文档 / 许可 / README / CHANGELOG / CI
cp ../README.md ../LICENSE ../CHANGELOG.md $OUT/ 2>/dev/null
cp ../docs/*.md $OUT/docs/ 2>/dev/null
cp ../.github/workflows/ci.yml $OUT/.github/workflows/ 2>/dev/null
cp ../graph.png ../texting.png ../image.png ../rending.png $OUT/ 2>/dev/null
cp ../screenshot_aa.png ../screenshot_gauge.png ../screenshot_checker.png ../screenshot_gradient.png $OUT/ 2>/dev/null

# 清理可能混入的构建产物/临时文件 (只删非源码)
find $OUT \( -name "*.o" -o -name "*.bak" -o -name "*.orig" -o -name "*.rej" -o -name "*~" \) -exec rm -rf {} + 2>/dev/null
for b in fxtk_sim headless_test render_canvas rt_resize; do rm -f $OUT/demo-main/test/$b $OUT/demo-main/$b 2>/dev/null; done

tar czf fxtk-$VER.tar.gz $OUT
echo "🎉 $VER 源码打包完成:"
echo "   .c 源文件: $(tar tzf fxtk-$VER.tar.gz | grep -c '\.c$')"
echo "   .h 头文件: $(tar tzf fxtk-$VER.tar.gz | grep -c '\.h$')"
echo "   LICENSE 在包内: $(tar tzf fxtk-$VER.tar.gz | grep -c 'LICENSE')  (应=1)"
echo "   canvas 示例: $(tar tzf fxtk-$VER.tar.gz | grep -c 'examples/canvas/')  (应=8: 7源码+目录)"
