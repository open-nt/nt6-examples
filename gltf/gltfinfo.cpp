// MIT License - see LICENSE
//
// Loads a glTF 2.0 file (.gltf or .glb) and prints what it holds: meshes and their triangles,
// materials, textures, cameras and lights, and the instances of the default scene with their
// world-space positions. A ray tracer builds one bottom-level acceleration structure per mesh and
// one top-level instance per mesh instance printed here.
//
//   gltfinfo DamagedHelmet.glb
//   gltfinfo Sponza/glTF/Sponza.gltf --instances

#include <QtCore>
#include <QtGltf>

#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Prints what a glTF 2.0 file holds."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("The .gltf or .glb file."));
    const QCommandLineOption instancesOption(QStringLiteral("instances"),
                                             QStringLiteral("List every instance of the default scene."));
    parser.addOption(instancesOption);
    parser.process(app);
    if (parser.positionalArguments().size() != 1)
        parser.showHelp(1);

    QtGltf::Document document;
    if (!document.load(parser.positionalArguments().first())) {
        std::fprintf(stderr, "gltfinfo: %s\n", qPrintable(document.errorString()));
        return 1;
    }
    for (const QtGltf::Mesh &mesh : document.meshes()) {
        for (const QtGltf::Primitive &primitive : mesh.primitives) {
            if (primitive.mode != QtGltf::Primitive::Triangles)
                continue;
            // positions, normals, texCoords0 and indices are ready to copy into vertex and index buffers.
            const qsizetype triangles = primitive.indices.size() / 3;
            Q_UNUSED(triangles);
        }
    }

    std::printf("generator:  %s\n", qPrintable(document.generator()));
    std::printf("extensions: %s\n", qPrintable(document.extensionsUsed().join(QStringLiteral(", "))));

    qsizetype vertices = 0;
    qsizetype triangles = 0;
    std::printf("\n%lld meshes\n", static_cast<long long>(document.meshes().size()));
    for (qsizetype i = 0; i < document.meshes().size(); ++i) {
        const QtGltf::Mesh &mesh = document.meshes().at(i);
        for (const QtGltf::Primitive &p : mesh.primitives) {
            vertices += p.positions.size();
            if (p.mode == QtGltf::Primitive::Triangles)
                triangles += p.indices.size() / 3;
        }
        if (i < 20)
            std::printf("  %lld %s: %lld primitives\n", static_cast<long long>(i), qPrintable(mesh.name),
                        static_cast<long long>(mesh.primitives.size()));
    }
    std::printf("  %lld vertices, %lld triangles\n", static_cast<long long>(vertices),
                static_cast<long long>(triangles));

    std::printf("\n%lld materials\n", static_cast<long long>(document.materials().size()));
    for (qsizetype i = 0; i < document.materials().size() && i < 20; ++i) {
        const QtGltf::Material &m = document.materials().at(i);
        std::printf("  %lld %s: base (%.2f %.2f %.2f %.2f) metallic %.2f roughness %.2f%s%s%s\n",
                    static_cast<long long>(i), qPrintable(m.name), double(m.baseColorFactor.x()),
                    double(m.baseColorFactor.y()), double(m.baseColorFactor.z()), double(m.baseColorFactor.w()),
                    double(m.metallicFactor), double(m.roughnessFactor),
                    m.baseColorTexture.isValid() ? " textured" : "", m.normalTexture.isValid() ? " normal-mapped" : "",
                    m.emissiveFactor.isNull() ? "" : " emissive");
    }
    std::printf("\n%lld textures, %lld images, %lld cameras, %lld lights\n",
                static_cast<long long>(document.textures().size()), static_cast<long long>(document.images().size()),
                static_cast<long long>(document.cameras().size()), static_cast<long long>(document.lights().size()));

    const QList<QtGltf::Instance> instances = document.instances();
    qsizetype meshInstances = 0;
    for (const QtGltf::Instance &instance : instances) {
        const QtGltf::Node &node = document.nodes().at(instance.node);
        if (node.mesh < 0)
            continue;
        // One top-level acceleration structure instance: node.mesh's geometry, placed by
        // instance.transform.
        ++meshInstances;
    }
    if (parser.isSet(instancesOption)) {
        for (const QtGltf::Instance &instance : instances) {
            const QtGltf::Node &node = document.nodes().at(instance.node);
            if (node.mesh < 0)
                continue;
            const QVector3D origin = instance.transform.map(QVector3D());
            std::printf("  node %d (%s): mesh %d at (%.2f, %.2f, %.2f)\n", instance.node, qPrintable(node.name),
                        node.mesh, double(origin.x()), double(origin.y()), double(origin.z()));
        }
    }
    std::printf("\nscene %d: %lld nodes, %lld mesh instances\n", document.defaultScene(),
                static_cast<long long>(instances.size()), static_cast<long long>(meshInstances));
    return 0;
}
