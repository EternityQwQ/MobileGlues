package net.irisshaders.iris.mixin.forge;

import com.llamalad7.mixinextras.sugar.Local;
import com.mojang.blaze3d.vertex.VertexConsumer;
import net.irisshaders.iris.shaderpack.materialmap.WorldRenderingSettings;
import net.irisshaders.iris.vertices.BlockSensitiveBufferBuilder;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.block.state.BlockState;
import net.neoforged.neoforge.client.event.AddSectionGeometryEvent;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Pseudo
@Mixin(targets = "com/portingdeadmods/cable_facades/events/GameClientEvents$2", remap = true)
public class MixinGameClientEvents {
	@Inject(method = "render", at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/block/BlockRenderDispatcher;renderBatched(Lnet/minecraft/world/level/block/state/BlockState;Lnet/minecraft/core/BlockPos;Lnet/minecraft/world/level/BlockAndTintGetter;Lcom/mojang/blaze3d/vertex/PoseStack;Lcom/mojang/blaze3d/vertex/VertexConsumer;ZLnet/minecraft/util/RandomSource;Lnet/neoforged/neoforge/client/model/data/ModelData;Lnet/minecraft/client/renderer/RenderType;)V"))
	private void iris$setId(AddSectionGeometryEvent.SectionRenderingContext sectionRenderingContext, CallbackInfo ci, @Local BlockState state, @Local VertexConsumer buffer, @Local BlockPos pos) {
		if (WorldRenderingSettings.INSTANCE.getBlockStateIds() == null) return;
		((BlockSensitiveBufferBuilder) buffer).beginBlock(WorldRenderingSettings.INSTANCE.getBlockStateIds().getInt(state), (byte) 0, (byte) state.getLightEmission(), pos.getX(), pos.getY(), pos.getZ());
	}
	@Inject(method = "render", at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/block/BlockRenderDispatcher;renderBatched(Lnet/minecraft/world/level/block/state/BlockState;Lnet/minecraft/core/BlockPos;Lnet/minecraft/world/level/BlockAndTintGetter;Lcom/mojang/blaze3d/vertex/PoseStack;Lcom/mojang/blaze3d/vertex/VertexConsumer;ZLnet/minecraft/util/RandomSource;Lnet/neoforged/neoforge/client/model/data/ModelData;Lnet/minecraft/client/renderer/RenderType;)V", shift = At.Shift.AFTER))
	private void iris$removeId(AddSectionGeometryEvent.SectionRenderingContext sectionRenderingContext, CallbackInfo ci, @Local BlockState state, @Local VertexConsumer buffer, @Local BlockPos pos) {
		if (WorldRenderingSettings.INSTANCE.getBlockStateIds() == null) return;
		((BlockSensitiveBufferBuilder) buffer).endBlock();
	}
}
