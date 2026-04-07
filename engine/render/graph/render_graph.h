#pragma once

#include <vector>
#include <functional>
#include <string>
#include <cstdint>
#include "render/public/renderer_api.h"

namespace jaeng {

// Render Graph v0 (single target, color + depth):
// - Declares passes that bind one or more color render targets.
// - Depth is optionally declared on passes and only support defaults target.
// - Uses only functions that exist in the current RendererAPI.

struct RGColorTarget {
    TextureHandle tex = 0;
    float clear_rgba[4] = {0, 0, 0, 1};
};

struct RGDepthTarget {
    TextureHandle tex = 0;      // 0 = unused, !0 = default
    float clear_depth = 1.0f;
};

struct RGPassContext {
    RendererAPI* gfx = nullptr;
    CommandListHandle cmd = 0;
    const RGColorTarget* colorTargets = nullptr;
    uint32_t colorCount = 0;
    const RGDepthTarget* depthTarget = nullptr;
};

using RecordFunc = std::function<void(const RGPassContext&)>;

struct RGPass {
    std::string name;
    std::vector<RGColorTarget> colorTargets;
    RGDepthTarget depthTarget;
    RecordFunc record;
};

class RenderGraph {
public:
    void reset() {
        passes_.clear();
    }

    // Add a pass; returns its index.
    uint32_t add_pass(const std::string& name,
                      const std::vector<RGColorTarget>& colors,
                      const RGDepthTarget& depth,
                      RecordFunc record) {
        passes_.emplace_back(RGPass{
            .name = name, .colorTargets = colors,
            .depthTarget = depth, .record = std::move(record)
        });
        return static_cast<uint32_t>(passes_.size() - 1);
    }

    // v0 compile: no-op (hook for validation later).
    bool compile() const {
        return true;
    }

    // Execute the graph for the current frame.
    void execute(RendererAPI& gfx, TextureHandle defaultDepth = 0, std::function<void(RendererAPI& gfx)> pre_record = nullptr) {
        if (!gfx.begin_commands || !gfx.cmd_begin_pass ||
            !gfx.cmd_end_pass || !gfx.end_commands || !gfx.submit) {
            return;
        }

        if (pre_record) {
            pre_record(gfx);
        }

        CommandListHandle cmd = gfx.begin_commands();
        if (cmd == 0) return;

        for (size_t pi = 0; pi < passes_.size(); ++pi) {
            const auto& pass = passes_[pi];
            
            auto load = (pi == 0)? LoadOp::Clear : LoadOp::Load;

            std::vector<ColorAttachmentDesc> atts;
            atts.reserve(pass.colorTargets.size());
            for (const auto& ct : pass.colorTargets) {
                ColorAttachmentDesc att{0};
                att.tex = ct.tex;
                att.clear_rgba[0] = ct.clear_rgba[0]; att.clear_rgba[1] = ct.clear_rgba[1];
                att.clear_rgba[2] = ct.clear_rgba[2]; att.clear_rgba[3] = ct.clear_rgba[3];
                atts.push_back(std::move(att));
            }

            DepthAttachmentDesc depthOps{};
            bool useDepth = (pass.depthTarget.tex != 0) || (defaultDepth != 0);
            if (useDepth) {
                depthOps.tex = (pass.depthTarget.tex != 0) ? pass.depthTarget.tex : defaultDepth;
                depthOps.clear_d = pass.depthTarget.clear_depth;
            }

            gfx.cmd_begin_pass(cmd, load, atts.data(), static_cast<uint32_t>(atts.size()), useDepth ? &depthOps : nullptr);

            if (pass.record) {
                RGPassContext ctx;
                ctx.gfx = &gfx;
                ctx.cmd = cmd;
                ctx.colorTargets = pass.colorTargets.empty() ? nullptr : pass.colorTargets.data();
                ctx.colorCount = static_cast<uint32_t>(pass.colorTargets.size());
                ctx.depthTarget = &pass.depthTarget;
                pass.record(ctx);
            }

            gfx.cmd_end_pass(cmd);
        }

        gfx.end_commands(cmd);
        gfx.submit(&cmd, 1);
    }

private:
    std::vector<RGPass> passes_;
};

} // namespace jaeng
