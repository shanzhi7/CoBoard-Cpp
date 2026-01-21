//该文件用来解析message.proto
//引入path模块,用于处理文件路径
const path = require('path') 

//引入官方的 gRPC Node.js 实现库 @grpc/grpc-js
const grpc = require('@grpc/grpc-js')

//引入 @grpc/proto-loader 工具，用于加载和解析 .proto 文件
const protoLoader = require('@grpc/proto-loader')        

//拼接proto文件路径,——dirname是当前文件夹路径
const PROTO_PATH = path.join(__dirname,'message.proto')

//同步加载并解析 .proto 文件，生成可供 gRPC 使用的protobuf包定义(package definition)
const packageDefinition = protoLoader.loadSync(PROTO_PATH,{keepCase:true,longs:'string',
    enums:'string',defaults:true,oneofs:true
})

//通过 protoLoader.loadSync 解析得到的 packageDefinition(Protobuf 包定义)加载为 gRPC 可直接使用的描述符对象 protoDescriptor
const protoDescriptor = grpc.loadPackageDefinition(packageDefinition)

//从 gRPC 描述符对象 protoDescriptor 中提取出 proto 文件中定义的 message 包
const message_proto = protoDescriptor.message

//导出message_proto
module.exports = message_proto