# Persistent serialization

Persistent models derive from `Ursa::Serializable`. Implement `SerializationType()`,
`SerializationVersion()`, `Serialize(ArchiveWriter&) const`, and
`Deserialize(const ArchiveReader&, SerializationContext&)`. Runtime device handles,
windows and GPU objects are rebuilt from persistent descriptions.

Use PascalCase type/method names and camelCase fields. Serialized field names and
stable type names are persistence contracts. Increment a schema version when its
meaning changes; factories reject unsupported versions rather than guessing.

## Components

The registration example below is compiled and exercised in
`UrsaSerializableHeaderSmoke` and `UrsaCustomComponentTests` (their components also implement the archive methods).

```cpp
return registry.RegisterSerializable<CounterComponent>("game.Counter", 1,
    [](const Ursa::GameObject::Ptr& owner, Ursa::ComponentLoadContext& context)
        -> Ursa::Result<CounterComponent*>
    {
        auto total = context.Service<int>();
        if (!total)
        {
            return Ursa::Result<CounterComponent*>::Failure(total.error);
        }
        return owner->AddComponent<CounterComponent>(*total.value, 0);
    });
```

Factories establish constructor dependencies. Callers supply them with
`context.Provide(service)` before loading. Component methods own their persistent
fields; registration supplies the stable type, version and factory.

## Archives

`ArchiveWriter::Write` supports booleans, signed/unsigned 64-bit integers, finite
numbers and strings. `Object`, `Array`, `AddObject`, and `Append` represent nested
data; `WriteBytes` represents a binary block. Reader operations return diagnostic
`Result` values on missing, malformed or out-of-range fields. Validate into local
values or a candidate model before replacing live state.

`JsonArchive::Encode/Decode` creates readable JSON with explicit binary-block tags.
`BinaryArchive::Encode/Decode` uses a versioned `URSAARC` envelope and CBOR, including
native byte strings. Both preserve the same fields. `UrsaArchiveTests` exercises
both formats, nested arrays, Unicode, blocks, and malformed inputs.

Resource envelopes retain their kind, resource ID and format version. Graph
serializers own tree traversal and envelopes; model methods own model fields.
`JsonArchiveInterop.hpp` is an envelope adapter, not a dependency custom models
need for implementing field serialization.

## References

Persist a `ResourceId`, never a pointer or runtime component handle. For references
within a graph, call `context.Defer(...)` during `Deserialize` and resolve with
`context.Object<T>(id)` inside that callback. The loader registers every object
and component before `ResolveReferences()`. This supports forward references.
`SnakePresentation` is a working example referencing a GameObject by ID.

Resource dependencies load through `context.ResourceLoader()`; save-time resource
identification uses `writer.Context()->Identify(object)`. Scene and PrefabAsset
archives need a component registry service and a `ComponentLoadContext` for graph
creation. Candidate graph failure leaves the active graph unchanged.
