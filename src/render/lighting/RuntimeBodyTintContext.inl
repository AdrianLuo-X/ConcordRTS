// A draw may borrow the immutable input when its sole override is unchanged.
// Storage stays local to the draw; nested calls never share mutable contexts.
class RuntimeBodyTintContext {
public:
    RuntimeBodyTintContext(const RuntimeMapRenderContext& original,
        bool chaosBodyTint, bool forceReference) : value_(&original) {
        if (forceReference || original.chaosBodyTint != chaosBodyTint) {
            storage_.emplace(original);
            storage_->chaosBodyTint = chaosBodyTint;
            value_ = &*storage_;
        }
    }
    RuntimeBodyTintContext(const RuntimeBodyTintContext&) = delete;
    RuntimeBodyTintContext& operator=(const RuntimeBodyTintContext&) = delete;
    const RuntimeMapRenderContext& get() const { return *value_; }
private:
    std::optional<RuntimeMapRenderContext> storage_;
    const RuntimeMapRenderContext* value_;
};