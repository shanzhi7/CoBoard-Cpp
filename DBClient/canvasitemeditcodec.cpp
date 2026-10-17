#include "canvasitemeditcodec.h"

message::ItemEditState CanvasItemEditCodec::Encode(const CanvasItemEditState& state)
{
    // 1. 公共变换使用局部原点和完整矩阵，不能用场景外接矩形替代旋转。
    message::ItemEditState result;
    result.set_item_id(state._item_id.toStdString());
    result.set_kind(static_cast<message::ItemKind>(state._kind));
    result.set_x(state._position.x());
    result.set_y(state._position.y());
    result.set_origin_x(state._origin.x());
    result.set_origin_y(state._origin.y());
    result.set_rotation(state._rotation);
    result.set_scale(state._scale);
    const QTransform& matrix = state._transform;
    for (double value : {matrix.m11(), matrix.m12(), matrix.m13(), matrix.m21(), matrix.m22(),
                         matrix.m23(), matrix.m31(), matrix.m32(), matrix.m33()})
    {
        result.add_matrix(value);
    }
    // 2. 只传类型对应的小型几何；路径保留创建数据，通过绝对映射更新。
    for (double value : state._geometry)
    {
        result.add_geometry(value);
    }
    result.set_text(state._text.toStdString());
    result.set_font_family(state._text_style._font_family.toStdString());
    result.set_font_size(state._text_style._font_size);
    result.set_color(state._text_style._color.rgba());
    result.set_bold(state._text_style._is_bold);
    return result;
}

bool CanvasItemEditCodec::Decode(const message::ItemEditState& input, CanvasItemEditState* state)
{
    // 1. 固定矩阵长度，不能让缺失字段变成不可逆的全零变换。
    if (!state || input.matrix_size() != 9)
    {
        return false;
    }
    CanvasItemEditState result;
    result._item_id = QString::fromStdString(input.item_id());
    result._kind = input.kind();
    result._position = QPointF(input.x(), input.y());
    result._origin = QPointF(input.origin_x(), input.origin_y());
    result._rotation = input.rotation();
    result._scale = input.scale();
    result._transform = QTransform(input.matrix(0), input.matrix(1), input.matrix(2),
        input.matrix(3), input.matrix(4), input.matrix(5), input.matrix(6), input.matrix(7), input.matrix(8));
    for (double value : input.geometry())
    {
        result._geometry.append(value);
    }
    // 2. 非文字图元的初始快照允许没有字体字段，使用与本地新建相同的默认值。
    result._text = QString::fromStdString(input.text());
    if (input.font_size() != 0)
    {
        result._text_style = {QString::fromStdString(input.font_family()), static_cast<int>(input.font_size()),
            QColor::fromRgba(input.color()), input.bold()};
    }
    // 3. 完整值校验通过后才交付快照，失败不会修改调用方已有状态。
    if (!CanvasItemEdit::IsValid(result))
    {
        return false;
    }
    *state = result;
    return true;
}

quint32 CanvasItemEditCodec::ChangedFields(const CanvasItemEditState& before, const CanvasItemEditState& after)
{
    // 1. 将文字与几何差异分别计算，移动不能提交过期的文字内容。
    quint32 fields = before._text != after._text || !(before._text_style == after._text_style) ? 2U : 0U;
    CanvasItemEditState geometry_before = before;
    geometry_before._text = after._text;
    geometry_before._text_style = after._text_style;
    if (geometry_before != after)
    {
        fields |= 1U;
    }
    return fields;
}
