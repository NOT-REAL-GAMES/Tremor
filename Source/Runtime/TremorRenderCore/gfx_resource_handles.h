#pragma once

#include "gfx_resource_types.h"

namespace tremor::gfx {

    class Texture : public Resource {
    public:
        virtual ~Texture() = default;
        virtual uint32_t getWidth() const = 0;
        virtual uint32_t getHeight() const = 0;
        virtual TextureFormat getFormat() const = 0;

    protected:
        Texture() = default;
    };

    class Shader : public Resource {
    public:
        virtual ~Shader() = default;
        virtual ShaderType getType() const = 0;

    protected:
        Shader() = default;
    };

    using TextureHandle = Handle<Texture>;
    using BufferHandle = Handle<Buffer>;
    using ShaderHandle = Handle<Shader>;
    using PipelineHandle = Handle<Pipeline>;

    template<typename To, typename From>
    Handle<To> handle_cast(const Handle<From>& handle) {
        return Handle<To>(dynamic_cast<To*>(handle.get()));
    }

} // namespace tremor::gfx
