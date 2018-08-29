const char vertex_src [] =
    "attribute vec4 position;\n"
    "attribute vec4 texcoords;\n"
    "varying vec2 textureCoordinate;\n"

    "void main()\n"
    "{\n"
    "    gl_Position = position;\n"
    "    textureCoordinate = texcoords.xy;\n"
    "}\n";

const char vertex_mvp_src [] =
    "attribute vec2 position;\n"
    "attribute vec2 texcoords;\n"
    "varying vec2 textureCoordinate;\n"
    "uniform mat4 transform;\n"

    "void main()\n"
    "{\n"
    "    gl_Position = transform * vec4(position, 0.0, 1.0);\n"
    "    textureCoordinate = texcoords.xy;\n"
    "}\n";

const char fragment_src [] =
    "varying highp vec2 textureCoordinate;\n"
    "uniform sampler2D texture;\n"

    "void main()\n"
    "{\n"
    "    gl_FragColor = texture2D(texture, textureCoordinate);\n"
    "}\n";

const char fragment_src_bgra [] =
    "varying highp vec2 textureCoordinate;\n"
    "uniform sampler2D texture;\n"

    "void main()\n"
    "{\n"
    "    gl_FragColor = texture2D(texture, textureCoordinate).bgra;\n"
    "}\n";
