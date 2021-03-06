﻿//
// ConvexVolumeTool.cpp
//
#include "ConvexVolumeTool.h"
#include "InputGeom.h"
#include "NavMeshTool.h"
#include "common/Utilities.h"

#include <Recast.h>
#include <RecastDebugDraw.h>
#include <DetourDebugDraw.h>

#include <imgui/imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_PLACEMENT_NEW
#include <imgui/imgui_internal.h>
#include <imgui/imgui_custom/imgui_user.h>
#include <glm/gtc/type_ptr.hpp>

#include <functional>

// Quick and dirty convex hull.

// Calculates convex hull on xz-plane of points on 'pts',
// stores the indices of the resulting hull in 'out' and
// returns number of points on hull.
static void convexhull(const std::vector<glm::vec3>& pts, std::vector<uint32_t>& out)
{
	// Find lower-leftmost point.
	uint32_t hull = 0;
	for (uint32_t i = 1; i < static_cast<uint32_t>(pts.size()); ++i)
		if (cmppt(pts[i], pts[hull]))
			hull = i;
	// Gift wrap hull.
	uint32_t endpt = 0;
	out.clear();
	do
	{
		out.push_back(hull);
		endpt = 0;
		for (uint32_t j = 1; j < static_cast<uint32_t>(pts.size()); ++j)
			if (hull == endpt || left(pts[hull], pts[endpt], pts[j]))
				endpt = j;
		hull = endpt;
	} while (endpt != out[0]);
}

static int pointInPoly(const std::vector<glm::vec3>& verts, const glm::vec3& p)
{
	bool c = false;

	for (size_t i = 0, j = verts.size() - 1; i < verts.size(); j = i++)
	{
		const glm::vec3& vi = verts[i];
		const glm::vec3& vj = verts[j];

		if (((vi[2] > p[2]) != (vj[2] > p[2])) &&
			(p[0] < (vj[0] - vi[0]) * (p[2] - vi[2]) / (vj[2] - vi[2]) + vi[0]))
		{
			c = !c;
		}
	}
	return c;
}

//----------------------------------------------------------------------------

ConvexVolumeTool::ConvexVolumeTool()
{
}

ConvexVolumeTool::~ConvexVolumeTool()
{
}

void ConvexVolumeTool::init(NavMeshTool* meshTool)
{
	m_meshTool = meshTool;

	m_state = (ConvexVolumeToolState*)m_meshTool->getToolState(type());
	if (!m_state)
	{
		m_state = new ConvexVolumeToolState();
		m_meshTool->setToolState(type(), m_state);
	}

	m_state->init(m_meshTool);
}

void ConvexVolumeTool::reset()
{
	m_editing = false;
}

static bool AreaTypeCombo(NavMesh* navMesh, uint8_t* areaType)
{
	const auto& polyAreas = navMesh->GetPolyAreas();
	bool changed = false;

	struct Iter {
		decltype(polyAreas)& polys;
	};
	Iter data{ polyAreas };

	static auto getter = [](void* data, int index, ImColor* color, const char** text) -> bool
	{
		Iter* p = (Iter*)data;

		*color = p->polys[index]->color;
		color->Value.w = 1.0f; // no transparency
		*text = p->polys[index]->name.c_str();
		return true;
	};

	int size = (int)polyAreas.size();
	int selected = 0;

	for (int i = 0; i < size; ++i)
	{
		if (polyAreas[i]->id == *areaType)
			selected = i;
	}

	if (ImGuiEx::ColorCombo("Area Type", &selected, getter, &data, size, 10))
	{
		*areaType = polyAreas[selected]->id;
		changed = true;
	}

	return changed;
}

void ConvexVolumeTool::handleMenu()
{
	auto navMesh = m_meshTool->GetNavMesh();
	if (!navMesh) return;

	// show list of existing convex volumes
	if (ImGui::CollapsingSubHeader("Help"))
	{
		ImGui::TextWrapped("Volumes can be used to mark parts of the map with different area types, including"
			" unwalkable areas. You can also create custom areas with modified travel costs, making certain"
			" areas cheaper or more expensive to travel. When planning paths, cheaper areas are preferred.\n");

		ImGui::TextWrapped("To create a new volume, click 'Create New'. To edit an existing volume, click on it"
			" in the list of volumes.\n");

		ImGui::TextWrapped("Click on the mesh to place points to create a volume. Alt-LMB or "
			"press 'Create Volume' to generate the volume from the points. Clear shape to cancel.");

		ImGui::Separator();
	}

	ImGui::Text("%d Volumes", navMesh->GetConvexVolumeCount());
	ImGui::BeginChild("VolumeList", ImVec2(0, 200), true);
	for (size_t i = 0; i < navMesh->GetConvexVolumeCount(); ++i)
	{
		ConvexVolume* volume = navMesh->GetConvexVolume(i);
		const PolyAreaType& area = navMesh->GetPolyArea(volume->areaType);

		char label[256];
		const char* volumeName = volume->name.empty() ? "unnamed" : volume->name.c_str();

		if (!area.valid)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImColor(255, 0, 0));

			sprintf_s(label, "%04d: %s (Invalid Area Type: %d)", volume->id, volumeName, volume->areaType);
		}
		else
		{
			if (area.name.empty())
				sprintf_s(label, "%04d: %s (Unnamed Area: %d)", volume->id, volumeName, volume->areaType);
			else
				sprintf_s(label, "%04d: %s (%s)", volume->id, volumeName, area.name.c_str());
		}

		bool selected = (m_state->m_currentVolumeId == volume->id);

		if (ImGui::Selectable(label, &selected))
		{
			if (selected)
			{
				m_state->reset();
				m_state->m_editVolume = *volume;
				m_state->m_currentVolumeId = volume->id;
				m_editing = false;
			}
		}

		if (!area.valid)
		{
			ImGui::PopStyleColor(1);
		}
	}
	ImGui::EndChild();

	{
		ImGui::BeginChild("##buttons", ImVec2(0, 30), false);
		ImGui::Columns(3, 0, false);

		if (ImGui::Button("Create New", ImVec2(-1, 0)))
		{
			m_state->reset();
			m_editing = true;
		}

		ImGui::NextColumn();
		ImGui::NextColumn();

		if (!m_editing
			&& m_state->m_currentVolumeId != 0
			&& ImGuiEx::ColoredButton("Delete", ImVec2(-1, 0), 0.0))
		{
			auto modifiedTiles = navMesh->GetTilesIntersectingConvexVolume(m_state->m_currentVolumeId);
			navMesh->DeleteConvexVolumeById(m_state->m_currentVolumeId);

			if (!modifiedTiles.empty())
			{
				m_meshTool->RebuildTiles(modifiedTiles);
			}
			m_state->m_currentVolumeId = 0;
		}

		ImGui::Columns(1);
		ImGui::EndChild();
	}

	if (m_editing)
	{
		ImGui::Text("Create New Volume");
		ImGui::Separator();

		ImGui::InputText("Name", m_state->m_name, 256);
		AreaTypeCombo(navMesh.get(), &m_state->m_areaType);
		ImGui::SliderFloat("Shape Height", &m_state->m_boxHeight, 0.1f, 100.0f);
		ImGui::SliderFloat("Shape Descent", &m_state->m_boxDescent, -100.f, 100.f);
		ImGui::SliderFloat("Poly Offset", &m_state->m_polyOffset, 0.0f, 10.0f);

#if 0 // Still WIP
		{
			auto& hull = m_state->m_hull;
			auto& pts = m_state->m_pts;
			ImGui::Text("%d Hull Points (%d total)", hull.size(), pts.size());

			ImGui::BeginChild("##points", ImVec2(0, 140), true);
			int i = 0;
			for (auto& pt : pts)
			{
				char text[16];
				sprintf_s(text, "%d", i);

				bool isHull = (std::find(hull.begin(), hull.end(), i) != hull.end());
				if (isHull)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImColor(255, 255, 0));
				}

				ImGui::InputFloat3(text, glm::value_ptr(pt), 1);

				if (isHull)
				{
					ImGui::PopStyleColor(1);
				}

				i++;
			}

			ImGui::EndChild();
		}
#endif

		ImGui::Columns(3, 0, false);
		if (m_state->m_hull.size() > 2 && ImGuiEx::ColoredButton("Create Volume", ImVec2(0, 0), 0.28f))
		{
			auto modifiedTiles = m_state->CreateShape();
			if (!modifiedTiles.empty())
			{
				m_meshTool->RebuildTiles(modifiedTiles);
			}
			m_editing = false;
		}
		ImGui::NextColumn();
		ImGui::NextColumn();
		if (ImGuiEx::ColoredButton("Cancel", ImVec2(-1, 0), 0.0))
		{
			m_state->reset();
			m_editing = false;
		}
		ImGui::Columns(1);
	}
	else if (m_state->m_currentVolumeId != 0)
	{
		ImGui::Text("Edit Volume");
		ImGui::Separator();

		char name[256];
		sprintf_s(name, m_state->m_editVolume.name.c_str());
		if (ImGui::InputText("Name", name, 256))
		{
			m_state->m_modified = true;
			m_state->m_editVolume.name = name;
		}

		m_state->m_modified |= AreaTypeCombo(navMesh.get(), &m_state->m_editVolume.areaType);

		m_state->m_modified |= ImGui::InputFloat("Hieght Min", &m_state->m_editVolume.hmin, 1.0, 10.0, 1);
		m_state->m_modified |= ImGui::InputFloat("Hieght Max", &m_state->m_editVolume.hmax, 1.0, 10.0, 1);

		if (m_state->m_modified && ImGui::Button("Save Changes"))
		{
			if (ConvexVolume* vol = navMesh->GetConvexVolumeById(m_state->m_currentVolumeId))
			{
				vol->areaType = m_state->m_editVolume.areaType;
				vol->hmin = m_state->m_editVolume.hmin;
				vol->hmax = m_state->m_editVolume.hmax;
				vol->name = m_state->m_editVolume.name;
				vol->verts = m_state->m_editVolume.verts;

				auto modifiedTiles = navMesh->GetTilesIntersectingConvexVolume(vol->id);
				if (!modifiedTiles.empty())
				{
					m_meshTool->RebuildTiles(modifiedTiles);
				}
			}
		}
	}
	else
	{
		m_state->m_currentVolumeId = 0;
	}
}

void ConvexVolumeTool::handleClick(const glm::vec3& /*s*/, const glm::vec3& p, bool shift)
{
	// if we're not editing a volume right now, switch to edit mode
	if (m_state->m_currentVolumeId == 0)
		m_editing = true;

	if (!m_editing) return;

	if (!m_meshTool) return;
	auto navMesh = m_meshTool->GetNavMesh();

	std::vector<dtTileRef> modifiedTiles = m_state->handleVolumeClick(p, shift);

	if (!modifiedTiles.empty())
	{
		m_meshTool->RebuildTiles(modifiedTiles);
	}
}

void ConvexVolumeTool::handleRender()
{
}

void ConvexVolumeTool::handleRenderOverlay(const glm::mat4& /*proj*/,
	const glm::mat4& /*model*/, const glm::ivec4& view)
{
	if (m_editing)
	{
		// Tool help
		if (m_state->m_pts.empty())
		{
			ImGui::RenderTextRight(-330, -(view[3] - 40), ImVec4(255, 255, 255, 192),
				"LMB: Create new shape.  SHIFT+LMB: Delete existing shape (click inside a shape).");
		}
		else
		{
			ImGui::RenderTextRight(-330, -(view[3] - 40), ImVec4(255, 255, 255, 192),
				"Click LMB to add new points. Alt+Click to finish the shape.");
			ImGui::RenderTextRight(-330, -(view[3] - 60), ImVec4(255, 255, 255, 192),
				"The shape will be convex hull of all added points.");
		}
	}
}

//----------------------------------------------------------------------------

void ConvexVolumeToolState::init(NavMeshTool* meshTool)
{
	m_meshTool = meshTool;
}

void ConvexVolumeToolState::reset()
{
	m_pts.clear();
	m_hull.clear();
	m_currentVolumeId = 0;
	m_modified = false;
	m_editVolume = ConvexVolume{};
	m_name[0] = 0;
}

void ConvexVolumeToolState::handleRender()
{
	DebugDrawGL dd;

	// Find height extents of the shape.
	float minh = FLT_MAX, maxh = 0;

	if (m_currentVolumeId != 0)
	{
		minh = m_editVolume.hmin;
		maxh = m_editVolume.hmax;

		dd.begin(DU_DRAW_POINTS, 4.0f);
		for (size_t i = 0; i < m_editVolume.verts.size(); ++i)
		{
			unsigned int col = duRGBA(255, 255, 255, 255);
			if (i == m_editVolume.verts.size() - 1)
				col = duRGBA(240, 32, 16, 255);
			dd.vertex(m_editVolume.verts[i].x,
				m_editVolume.verts[i].y + 0.1f,
				m_editVolume.verts[i].z, col);
		}
		dd.end();

		dd.begin(DU_DRAW_LINES, 2.0f);
		for (size_t i = 0, j = m_editVolume.verts.size() - 1; i < m_editVolume.verts.size(); j = i++)
		{
			const glm::vec3& vi = m_editVolume.verts[j];
			const glm::vec3& vj = m_editVolume.verts[i];
			dd.vertex(vj.x, minh, vj.z, duRGBA(255, 255, 255, 64));
			dd.vertex(vi.x, minh, vi.z, duRGBA(255, 255, 255, 64));
			dd.vertex(vj.x, maxh, vj.z, duRGBA(255, 255, 255, 64));
			dd.vertex(vi.x, maxh, vi.z, duRGBA(255, 255, 255, 64));
			dd.vertex(vj.x, minh, vj.z, duRGBA(255, 255, 255, 64));
			dd.vertex(vj.x, maxh, vj.z, duRGBA(255, 255, 255, 64));
		}
		dd.end();
	}
	else
	{
		for (size_t i = 0; i < m_pts.size(); ++i)
			minh = glm::min(minh, m_pts[i].y);
		minh -= m_boxDescent;
		maxh = minh + m_boxHeight;

		dd.begin(DU_DRAW_POINTS, 4.0f);
		for (size_t i = 0; i < m_pts.size(); ++i)
		{
			unsigned int col = duRGBA(255, 255, 255, 255);
			if (i == m_pts.size() - 1)
				col = duRGBA(240, 32, 16, 255);
			dd.vertex(m_pts[i].x, m_pts[i].y + 0.1f, m_pts[i].z, col);
		}
		dd.end();

		dd.begin(DU_DRAW_LINES, 2.0f);
		for (size_t i = 0, j = m_hull.size() - 1; i < m_hull.size(); j = i++)
		{
			const glm::vec3& vi = m_pts[m_hull[j]];
			const glm::vec3& vj = m_pts[m_hull[i]];
			dd.vertex(vj.x, minh, vj.z, duRGBA(255, 255, 255, 64));
			dd.vertex(vi.x, minh, vi.z, duRGBA(255, 255, 255, 64));
			dd.vertex(vj.x, maxh, vj.z, duRGBA(255, 255, 255, 64));
			dd.vertex(vi.x, maxh, vi.z, duRGBA(255, 255, 255, 64));
			dd.vertex(vj.x, minh, vj.z, duRGBA(255, 255, 255, 64));
			dd.vertex(vj.x, maxh, vj.z, duRGBA(255, 255, 255, 64));
		}
		dd.end();
	}
}

void ConvexVolumeToolState::handleRenderOverlay(const glm::mat4& proj,
	const glm::mat4& model, const glm::ivec4& view)
{
}

std::vector<dtTileRef> ConvexVolumeToolState::handleVolumeClick(const glm::vec3& p, bool shift)
{
	auto navMesh = m_meshTool->GetNavMesh();
	std::vector<dtTileRef> modifiedTiles;

	if (shift)
	{
		// Delete
		size_t nearestIndex = -1;
		const auto& volumes = navMesh->GetConvexVolumes();
		size_t i = 0;
		for (const auto& vol : volumes)
		{
			if (pointInPoly(vol->verts, p) && p.y >= vol->hmin && p.y <= vol->hmax)
			{
				nearestIndex = i;
				break;
			}

			i++;
		}

		// If end point close enough, delete it.
		if (nearestIndex != -1)
		{ 
			const ConvexVolume* volume = navMesh->GetConvexVolume(nearestIndex);
			modifiedTiles = navMesh->GetTilesIntersectingConvexVolume(volume->id);
			m_meshTool->GetNavMesh()->DeleteConvexVolumeById(volume->id);
		}
	}
	else
	{
		// Create
		bool alt = (SDL_GetModState() & KMOD_ALT) != 0;

		// If clicked on that last pt, create the shape.
		if (m_pts.size() > 0 && (alt || distSqr(p, m_pts[m_pts.size() - 1]) < rcSqr(0.2f)))
		{
			modifiedTiles = CreateShape();
		}
		else
		{
			// Add new point
			m_pts.push_back(p);

			// Update hull.
			if (m_pts.size() >= 2)
				convexhull(m_pts, m_hull);
			else
				m_hull.clear();
		}
	}

	return modifiedTiles;
}

std::vector<dtTileRef> ConvexVolumeToolState::CreateShape()
{
	std::vector<dtTileRef> modifiedTiles;
	auto navMesh = m_meshTool->GetNavMesh();

	if (m_hull.size() > 2)
	{
		std::vector<glm::vec3> verts(m_hull.size());

		// Create shape.
		for (size_t i = 0; i < m_hull.size(); ++i)
			verts[i] = m_pts[m_hull[i]];

		float minh = FLT_MAX, maxh = 0;
		for (size_t i = 0; i < m_hull.size(); ++i)
			minh = glm::min(minh, verts[i].y);

		minh -= m_boxDescent;
		maxh = minh + m_boxHeight;

		if (m_polyOffset > 0.01f)
		{
			std::vector<glm::vec3> offset(m_hull.size() * 2 + 1);

			int noffset = rcOffsetPoly(glm::value_ptr(verts[0]), static_cast<int>(verts.size()),
				m_polyOffset, glm::value_ptr(offset[0]), static_cast<int>(verts.size() * 2 + 1));
			if (noffset > 0)
			{
				offset.resize(noffset);

				ConvexVolume* volume = navMesh->AddConvexVolume(offset, m_name, minh, maxh, (uint8_t)m_areaType);
				modifiedTiles = navMesh->GetTilesIntersectingConvexVolume(volume->id);
			}
		}
		else
		{
			ConvexVolume* volume = navMesh->AddConvexVolume(verts, m_name, minh, maxh, (uint8_t)m_areaType);
			modifiedTiles = navMesh->GetTilesIntersectingConvexVolume(volume->id);
		}
	}

	reset();

	return modifiedTiles;
}
