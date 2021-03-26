#include <Utopia/Core/AssetMngr.h>

#include <Utopia/Core/Serializer.h>

#include <fstream>

using namespace Ubpa::Utopia;
using namespace Ubpa::UDRefl;
using namespace Ubpa;

struct AssetMngr::Impl {
	// N asset <-> guid <-> path
	//               |
	//               v
	//            importer
	//
	// ext -> importer creator

	std::map<std::filesystem::path, xg::Guid> path2guid; // relative path
	std::unordered_map<xg::Guid, std::filesystem::path> guid2path; // relative path

	std::unordered_map<void*, xg::Guid> assetID2guid;
	std::unordered_map<void*, std::string> assetID2name;
	std::multimap<xg::Guid, UDRefl::SharedObject> guid2asset;

	std::unordered_map<xg::Guid, std::shared_ptr<AssetImporter>> guid2importer;

	std::map<std::filesystem::path, std::shared_ptr<AssetImporterCreator>> ext2creator;

	std::filesystem::path root{ L".." };

	static std::string LoadText(const std::filesystem::path& path);
	static rapidjson::Document LoadJSON(const std::filesystem::path& metapath);

	xg::Guid ImportAsset(const std::filesystem::path& path) {
		if (path == root)
			return {};

		auto relpath = std::filesystem::relative(path, root);
		if (relpath.empty() || *relpath.c_str() == L'.')
			return {};

		const auto ext = path.extension();

		assert(ext != ".meta");

		auto target = path2guid.find(relpath);
		if (target != path2guid.end())
			return target->second;

		if (!std::filesystem::exists(path))
			return {};

		auto metapath = std::filesystem::path{ path }.concat(".meta");
		bool existMeta = std::filesystem::exists(metapath);
		assert(!existMeta || !std::filesystem::is_directory(metapath));

		xg::Guid guid;
		std::shared_ptr<AssetImporter> importer;
		if (!existMeta) {
			auto ctarget = ext2creator.find(ext);
			if (ctarget == ext2creator.end()) {
				// default
				guid = xg::newGuid();
				DefaultAssetImporterCreator ctor;
				importer = ctor.CreateAssetImporter(guid);
				std::string metastr = Serializer::Instance().Serialize(importer->This());
				std::ofstream ofs(metapath);
				assert(ofs.is_open());
				ofs << metastr;
				ofs.close();
			}
			else {
				guid = xg::newGuid();
				importer = ctarget->second->CreateAssetImporter(guid);
				std::string metastr = Serializer::Instance().Serialize(importer->This());

				std::ofstream ofs(metapath);
				assert(ofs.is_open());
				ofs << metastr;
				ofs.close();
			}
		}
		else {
			std::string importerJson = Impl::LoadText(metapath);
			auto ctarget = ext2creator.find(ext);
			if (ctarget == ext2creator.end()) {
				DefaultAssetImporterCreator ctor;
				importer = ctor.DeserializeAssetImporter(importerJson);
			}
			else
				importer = ctarget->second->DeserializeAssetImporter(importerJson);
			guid = importer->GetGuid();
		}

		path2guid.emplace(relpath, guid);
		guid2path.emplace(guid, relpath);
		guid2importer.emplace(guid, importer);

		ImportAsset(path.parent_path());

		return guid;
	}

	std::filesystem::path GetFullPath(const std::filesystem::path& relpath) const {
		assert(relpath.is_relative());
		auto path = root;
		path += LR"(\)";
		path += relpath;
		return path;
	}
};

AssetMngr::AssetMngr()
	: pImpl{ new Impl }
{
	AssetImporter::RegisterToUDRefl();
	Serializer::Instance().RegisterSerializeFunction([](const AssetImporter* aimporter, Serializer::SerializeContext& ctx) {
		aimporter->Serialize(ctx);
	});
}

AssetMngr::~AssetMngr() {
	delete pImpl;
}

void AssetMngr::RegisterAssetImporterCreator(std::string_view extension, std::shared_ptr<AssetImporterCreator> creator) {
	pImpl->ext2creator.insert_or_assign(std::filesystem::path{ extension }, creator);
}

const std::filesystem::path& AssetMngr::GetRootPath() const noexcept {
	return pImpl->root;
}

void AssetMngr::SetRootPath(std::filesystem::path path) {
	Clear();
	pImpl->root = std::move(path);
}

void AssetMngr::Clear() {
	pImpl->assetID2guid.clear();
	pImpl->assetID2name.clear();
	pImpl->path2guid.clear();
	pImpl->guid2asset.clear();
	pImpl->guid2path.clear();
}

bool AssetMngr::IsImported(const std::filesystem::path& path) const {
	return pImpl->path2guid.contains(path);
}

xg::Guid AssetMngr::AssetPathToGUID(const std::filesystem::path& path) const {
	auto target = pImpl->path2guid.find(path);
	return target == pImpl->path2guid.end() ? xg::Guid{} : target->second;
}

bool AssetMngr::Contains(UDRefl::SharedObject obj) const {
	return pImpl->assetID2guid.contains(obj.GetPtr());
}

std::vector<xg::Guid> AssetMngr::FindAssets(const std::wregex& matchRegex) const {
	std::vector<xg::Guid> rst;
	for (const auto& [path, guid] : pImpl->path2guid) {
		if (std::regex_match(path.c_str(), matchRegex))
			rst.push_back(guid);
	}
	return rst;
}

xg::Guid AssetMngr::GetAssetGUID(UDRefl::SharedObject obj) const {
	auto target = pImpl->assetID2guid.find(obj.GetPtr());
	if (target == pImpl->assetID2guid.end())
		return {};
	return target->second;
}

const std::filesystem::path& AssetMngr::GetAssetPath(SharedObject obj) const {
	return GUIDToAssetPath(GetAssetGUID(obj));
}

Ubpa::Type AssetMngr::GetAssetType(const std::filesystem::path& path) const {
	auto guid = AssetPathToGUID(path);
	if (!guid.isValid())
		return {};
	auto target = pImpl->guid2asset.find(guid);
	if (target == pImpl->guid2asset.end())
		return {};

	return target->second.GetType();
}

const std::filesystem::path& AssetMngr::GUIDToAssetPath(const xg::Guid& guid) const {
	static const std::filesystem::path ERROR;
	auto target = pImpl->guid2path.find(guid);
	return target == pImpl->guid2path.end() ? ERROR : target->second;
}

SharedObject AssetMngr::GUIDToAsset(const xg::Guid& guid) const {
	auto target = pImpl->guid2asset.find(guid);
	if (target == pImpl->guid2asset.end())
		return {};

	return target->second;
}

SharedObject AssetMngr::GUIDToAsset(const xg::Guid& guid, Type type) const {
	auto iter_begin = pImpl->guid2asset.lower_bound(guid);
	auto iter_end = pImpl->guid2asset.upper_bound(guid);
	for (auto iter = iter_begin; iter != iter_end; ++iter) {
		if (iter->second.GetType() == type)
			return iter->second;
	}

	return {};
}

UDRefl::SharedObject AssetMngr::GUIDToAsset(const xg::Guid& guid, std::string_view name) const {
	auto iter_begin = pImpl->guid2asset.lower_bound(guid);
	auto iter_end = pImpl->guid2asset.upper_bound(guid);
	for (auto iter = iter_begin; iter != iter_end; ++iter) {
		if (pImpl->assetID2name.at(iter->second.GetPtr()) == name)
			return iter->second;
	}

	return {};
}

xg::Guid AssetMngr::ImportAsset(const std::filesystem::path& path) {
	assert(path.is_relative());

	if (path.empty() || *path.c_str() == L'.')
		return {};

	const auto ext = path.extension();

	 std::filesystem::path fullpath = pImpl->GetFullPath(path);

	 return pImpl->ImportAsset(fullpath);
}

void AssetMngr::ImportAssetRecursively(const std::filesystem::path& directory) {
	auto fullDir = std::filesystem::path{ GetRootPath() } += directory;

	if (!std::filesystem::is_directory(fullDir))
		return;

	for (const auto& entry : std::filesystem::recursive_directory_iterator(fullDir)) {
		const auto& path = entry.path();
		if (path == GetRootPath())
			continue;

		if(path.extension() == ".meta")
			continue;

		pImpl->ImportAsset(path);
	}
}

SharedObject AssetMngr::LoadMainAsset(const std::filesystem::path& path) {
	auto guid = ImportAsset(path);
	if (!guid.isValid())
		return {};

	auto target = pImpl->guid2asset.find(guid);
	if (target != pImpl->guid2asset.end())
		return target->second;
	auto ctx = pImpl->guid2importer.at(guid)->ImportAsset();
	if (ctx.GetAssets().empty())
		return {};
	auto mainObj = ctx.GetMainObject();
	for (const auto& [n, obj] : ctx.GetAssets()) {
		pImpl->assetID2guid.emplace(obj.GetPtr(), guid);
		pImpl->assetID2name.emplace(obj.GetPtr(), n);
		if (obj == mainObj)
			continue;
		pImpl->guid2asset.emplace(guid, obj);
	}
	if (mainObj) {
		pImpl->guid2asset.emplace(guid, mainObj);
	}

	return mainObj;
}

SharedObject AssetMngr::LoadAsset(const std::filesystem::path& path, Type type) {
	auto guid = ImportAsset(path);
	if (!guid.isValid())
		return {};

	if (!pImpl->guid2asset.contains(guid)) {
		auto ctx = pImpl->guid2importer.at(guid)->ImportAsset();
		if (ctx.GetAssets().empty())
			return {};
		auto mainObj = ctx.GetMainObject();
		for (const auto& [n, obj] : ctx.GetAssets()) {
			pImpl->assetID2guid.emplace(obj.GetPtr(), guid);
			pImpl->assetID2name.emplace(obj.GetPtr(), n);
			if (obj == mainObj)
				continue;
			pImpl->guid2asset.emplace(guid, obj);
		}
		if (mainObj)
			pImpl->guid2asset.emplace(guid, mainObj);
	}

	auto [iter_begin, iter_end] = pImpl->guid2asset.equal_range(guid);
	for (auto cursor = iter_begin; cursor != iter_end; ++cursor) {
		if (cursor->second.GetType() == type)
			return cursor->second;
	}
	return {};
}

std::vector<SharedObject> AssetMngr::LoadAllAssets(const std::filesystem::path& path) {
	auto guid = ImportAsset(path);
	if (!guid.isValid())
		return {};

	if (!pImpl->guid2asset.contains(guid)) {
		auto ctx = pImpl->guid2importer.at(guid)->ImportAsset();
		if (ctx.GetAssets().empty())
			return {};
		auto mainObj = ctx.GetMainObject();
		for (const auto& [n, obj] : ctx.GetAssets()) {
			pImpl->assetID2guid.emplace(obj.GetPtr(), guid);
			pImpl->assetID2name.emplace(obj.GetPtr(), n);
			if (obj == mainObj)
				continue;
			pImpl->guid2asset.emplace(guid, obj);
		}
		if (mainObj)
			pImpl->guid2asset.emplace(guid, mainObj);
	}

	auto [iter_begin, iter_end] = pImpl->guid2asset.equal_range(guid);
	std::vector<SharedObject> rst;
	for (auto cursor = iter_begin; cursor != iter_end; ++cursor)
		rst.push_back(cursor->second);
	return rst;
}

bool AssetMngr::DeleteAsset(const std::filesystem::path& path) {
	auto guid = AssetPathToGUID(path);
	if (guid.isValid()) {
		pImpl->guid2path.erase(guid);
		pImpl->path2guid.erase(path);
		pImpl->guid2importer.erase(guid);
		auto [iter_begin, iter_end] = pImpl->guid2asset.equal_range(guid);
		for (auto cursor = iter_begin; cursor != iter_end; ++cursor) {
			auto asset = cursor->second;
			pImpl->assetID2guid.erase(asset.GetPtr());
			pImpl->assetID2name.erase(asset.GetPtr());
		}
		pImpl->guid2asset.erase(iter_begin, iter_end);
	}
	auto fullpath = pImpl->GetFullPath(path);
	std::filesystem::remove_all(fullpath);
	std::filesystem::remove(fullpath.concat(".meta"));
	return true;
}

bool AssetMngr::CreateAsset(SharedObject ptr, const std::filesystem::path& path) {
	DeleteAsset(path);

	const auto ext = path.extension().string();
	assert(ext != ".meta");
	auto ctarget = pImpl->ext2creator.find(ext);
	if (ctarget == pImpl->ext2creator.end())
		return false;

	xg::Guid guid = xg::newGuid();
	auto importer = ctarget->second->CreateAssetImporter(guid);
	pImpl->guid2asset.emplace(guid, ptr);
	pImpl->guid2path.emplace(guid, path);
	pImpl->path2guid.emplace(path, guid);
	pImpl->assetID2guid.emplace(ptr.GetPtr(), guid);
	pImpl->assetID2name.emplace(ptr.GetPtr(), path.stem().string());
	pImpl->guid2importer.emplace(guid, importer);

	ImportAsset(path.parent_path());

	ReserializeAsset(path);

	return true;
}

bool AssetMngr::ReserializeAsset(const std::filesystem::path& path) {
	auto guid = AssetPathToGUID(path);
	if (!guid.isValid())
		return false;
	auto target = pImpl->guid2importer.find(guid);
	if (target == pImpl->guid2importer.end())
		return false;

	auto importer = target->second;
	auto fullpath = pImpl->GetFullPath(path);

	{
		auto json = importer->ReserializeAsset();
		if (json.empty())
			return false;
		std::ofstream ofs(fullpath);
		assert(ofs.is_open());
		ofs << json;
		ofs.close();
	}

	{
		auto importerjson = Serializer::Instance().Serialize(importer->This());
		std::ofstream ofs(fullpath.concat(".meta"));
		assert(ofs.is_open());
		ofs << importerjson;
		ofs.close();
	}

	return true;
}

bool AssetMngr::MoveAsset(const std::filesystem::path& src, const std::filesystem::path& dst) {
	if (!dst.is_relative())
		return false;

	if (std::filesystem::exists(dst))
		return false;

	auto srcguidtarget = pImpl->path2guid.find(src);
	if (srcguidtarget == pImpl->path2guid.end())
		return false;

	auto target = pImpl->path2guid.find(src);
	auto guid = target->second;
	try {
		std::filesystem::rename(src, dst);
		std::filesystem::rename(src.wstring() + L".meta", dst.wstring() + L".meta");
	}
	catch (...) {
		return false;
	}

	pImpl->guid2path.at(guid) = dst;
	pImpl->path2guid.erase(srcguidtarget);
	pImpl->path2guid.emplace(dst, guid);
	
	return true;
}

std::string_view AssetMngr::NameofAsset(UDRefl::SharedObject obj) const {
	auto target = pImpl->assetID2name.find(obj.GetPtr());
	if (target == pImpl->assetID2name.end())
		return {};

	return target->second;
}

void AssetMngr::SetImporterOverride(const std::filesystem::path& path, std::shared_ptr<AssetImporter> importer) {
	auto guid = AssetPathToGUID(path);
	if (!guid.isValid())
		return;
	pImpl->guid2importer.insert_or_assign(guid, importer);
	ReserializeAsset(path);
}

std::string AssetMngr::Impl::LoadText(const std::filesystem::path& path) {
	std::ifstream t(path);
	std::string str;

	t.seekg(0, std::ios::end);
	str.reserve(t.tellg());
	t.seekg(0, std::ios::beg);

	str.assign(
		std::istreambuf_iterator<char>(t),
		std::istreambuf_iterator<char>()
	);

	return str;
}