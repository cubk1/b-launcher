#include "app/downloads/Dependencies.h"
#include "app/downloads/Recent.h"
#include "app/system/AsyncTasks.h"

#include <map>

namespace Dependencies {

    namespace blb = b_launcher_base;
    using Addons::Status;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        constexpr size_t KEPT = 256;

        AsyncTasks tasks;
        std::map<std::string, Node> nodes;
        Recent nodeUse;
    }

    const Node& resolve(Addons::Source source, const Addons::Dependency& dependency, const std::string& gameVersion,
                        const std::string& loader) {
        std::string key = std::to_string((int) source) + "|" + dependency.projectId + "|" + dependency.versionId + "|" +
                gameVersion + "|" + loader;
        nodeUse.use(key);
        auto found = nodes.find(key);
        if (found != nodes.end()) {
            return found->second;
        }
        Node& entry = nodes[key];
        std::string project = dependency.projectId;
        std::string pinned = dependency.versionId;
        tasks.run(
                [key, source, project, pinned, gameVersion, loader](blb::cancellation_token token) -> Continuation {
                    Addons::Fetched fetched = !pinned.empty()
                            ? Addons::fetchVersion(source, pinned, token)
                            : Addons::fetchProjectVersions(source, project, gameVersion, loader, token);
                    if (token.is_cancelled()) {
                        return nullptr;
                    }
                    Node node;
                    if (!fetched.ok || fetched.versions.empty()) {
                        node.status = Status::Failed;
                        node.error = fetched.error.empty() ? std::string("没有兼容版本") : fetched.error;
                    } else {
                        node.status = Status::Ready;
                        node.version = std::move(fetched.versions.front());
                        node.version.source = source;
                    }
                    return [key, node = std::move(node)]() mutable { nodes[key] = std::move(node); };
                },
                [key](const std::string& error) -> Continuation {
                    return [key, error] {
                        nodes[key].status = Status::Failed;
                        nodes[key].error = error;
                    };
                });
        return entry;
    }

    void poll() {
        tasks.poll();
        nodeUse.trim(nodes, KEPT, [](const Node& node) { return node.status != Status::Loading; });
    }

    void shutdown() {
        tasks.stop();
    }
}
